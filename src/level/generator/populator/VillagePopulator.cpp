// SPDX-License-Identifier: LGPL-3.0-or-later
#include "VillagePopulator.h"
#include "level/generator/TerrainShape.h"
#include "level/generator/biome/Biome.h"
#include "level/generator/biome/BiomeRegistry.h"
#include "level/generator/noise/GenRandom.h"
#include "level/generator/structure/village/VillagePalette.h"
#include "level/generator/structure/village/VillagePieces.h"
#include "block/BlockIds.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace cursestone::level::generator::populator {

namespace {

using cursestone::block::BlockId;
using cursestone::level::generator::structure::Structure;
namespace village = cursestone::level::generator::structure::village;
namespace biome = cursestone::level::generator::biome;

// ---- Vanilla placement constants -----------------------------------------
// Vanilla village placement: a candidate chunk every kSpacing x kSpacing
// chunks, kept kSeparation chunks away from the region edge, salt 10387312.
constexpr int kSpacing = 32;
constexpr int kSeparation = 8;
constexpr int64_t kSalt = 10387312;

// Loose bound around the well before the layout is even built: the
// furthest village cell reaches ~40 blocks from the well corner plus the
// in-chunk offset, so 64 + the 16-block chunk is a safe overestimate.
constexpr int kVillageRadius = 64;

// How uneven the ground may be under a piece's footprint (same gate the
// desert temple uses), and how far a road column may sit from the well's
// own ground level before the road stops (vanilla roads give up on a
// cliff too).
constexpr int kMaxGroundStep = 2;
constexpr int kMaxPathStep = 5;

// Positive modulo so region offsets stay correct west of the origin.
int wrapRegion(int value) {
    int v = value % kSpacing;
    return v < 0 ? v + kSpacing : v;
}

// The chunk a region puts its village anchor in. Pure function of
// (seed, regionX, regionZ) — copied from StructurePopulator so both
// structure types share the exact same rigid placement math.
void regionCandidate(int64_t seed, int regionX, int regionZ, int salt,
                     int& chunkX, int& chunkZ) {
    GenRandom rng(seed * 341873128712LL +
                  (int64_t)regionX * 341873128712LL +
                  (int64_t)regionZ * 132897987541LL +
                  (int64_t)salt);
    const int span = kSpacing - kSeparation;
    chunkX = regionX * kSpacing +
             wrapRegion(regionX * kSpacing + kSeparation + (int)rng.nextInt(span));
    chunkZ = regionZ * kSpacing +
             wrapRegion(regionZ * kSpacing + kSeparation + (int)rng.nextInt(span));
}

// Biomes a village may start in and grow through. Vanilla 1.9-era
// villages spawn in plains, desert and savanna; the raised plains
// variant counts as plains. Desert HILLS, beaches, rivers and forests
// reject the village — that gate is what keeps a village an open
// grassland/sand thing instead of one crawling through a jungle.
bool isVillageBiome(uint8_t b) {
    using namespace biome;
    return b == BIOME_PLAINS || b == BIOME_PLAINS_PLATEAU ||
           b == BIOME_DESERT || b == BIOME_SAVANNA;
}

// ---- Geometry helpers -----------------------------------------------------
//
// Layout math works in two frames: the VILLAGE frame (origin = the
// well's top-left corner, axis-aligned with the world) and WORLD (the
// village frame rotated by the village's own quarter turn around its
// origin, then translated to the anchor). Both rotations use the same
// corner transform, so every box and every road cell moves together and
// the village stays internally consistent.
//
// One clockwise quarter turn of a point, viewed from above (+X east,
// +Z south): (x, z) -> (-z, x). Applied to the four corners of a box,
// the bounding box of the result is the rotated box.
void rotateRect(int& x, int& z, int& w, int& h, int rot) {
    int ax[4] = {x, x + w, x, x + w};
    int az[4] = {z, z, z + h, z + h};
    for (int r = 0; r < (rot & 3); ++r)
        for (int i = 0; i < 4; ++i) {
            const int nx = -az[i], nz = ax[i];
            ax[i] = nx;
            az[i] = nz;
        }
    int x0 = ax[0], x1 = ax[0], z0 = az[0], z1 = az[0];
    for (int i = 1; i < 4; ++i) {
        x0 = std::min(x0, ax[i]); x1 = std::max(x1, ax[i]);
        z0 = std::min(z0, az[i]); z1 = std::max(z1, az[i]);
    }
    x = x0; z = z0; w = x1 - x0; h = z1 - z0;
}

// The four road arms leaving the well. Each arm's buildings live on ONE
// side only (its own quadrant), which is what makes overlaps between
// arms impossible by construction:
//
//   east:  road x>=6 along +x, buildings south of it (z >= 3), doors face north
//   north: road z<=-1 along -z, buildings east of it (x >= 3), doors face west
//   west:  road x<=-1 along -x, buildings north of it (z <= 0), doors face south
//   south: road z>=6 along +z, buildings west of it (x <= 0), doors face east
//
// Roads occupy two lanes just off the well (rows {1,2} on either axis),
// and a 3-wide pad for the first two steps out of the well face.
struct ArmSpec {
    int dir;        // +1 runs from the well's high edge outward, -1 from the low edge
    bool alongX;    // east/west arms run along x, north/south along z
    int perpSide;   // +1 buildings on the high perpendicular side, -1 on the low side
    int doorTarget; // java facing each door must point at (toward the road)
};
constexpr ArmSpec kArms[4] = {
    {+1, true,  +1, 4},   // east
    {-1, false, +1, 2},   // north
    {-1, true,  -1, 3},   // west
    {+1, false, -1, 1},   // south
};

// First coordinate of an arm at along-distance t (t=0 is the well face).
inline int alongCoord(int dir, int t) { return dir > 0 ? 6 + t : -1 - t; }

// Half-open [start, start+ext) span of `ext` cells starting at t.
inline int alongStart(int dir, int t, int ext) {
    return dir > 0 ? 6 + t : -t - ext;
}

// Perpendicular band start for a building/lamp of extent ext on the
// arm's side: side +1 begins at row 3 (just south/east of the road),
// side -1 ends at row 0 (just north/west of it).
inline int perpStart(int side, int ext) { return side > 0 ? 3 : 1 - ext; }

// ---- The layout ------------------------------------------------------------
//
// Pure function of (seed, regionX, regionZ): no terrain, no biome, no
// chunk state. Every chunk that can see the village recomputes exactly
// this and stamps its own columns through it. The plan type itself is
// public (VillagePopulator.h) so verification tools can rebuild the
// same layout populate() stamps.

// Vanilla's weighted village piece pool (StructureVillagePieces). The
// one-shot buildings are capped at one copy the way vanilla caps them.
struct PoolEntry { int kind; int weight; };
constexpr PoolEntry kPool[] = {
    {village::PIECE_T1_HOUSE, 4}, {village::PIECE_T2_HOUSE, 4},
    {village::PIECE_T3_HOUSE, 3}, {village::PIECE_T4_HOUSE, 3},
    {village::PIECE_T5_HOUSE, 2},
    {village::PIECE_T1_FARM, 4},  {village::PIECE_T2_FARM, 3},
    {village::PIECE_CHURCH, 2},   {village::PIECE_LIBRARY, 2},
    {village::PIECE_BLACKSMITH, 2}, {village::PIECE_BUTCHER, 2},
};
constexpr int kPoolTotal = 31;

// Entrance wall of a template, in java compass values, straight out of
// the generated piece table (the flips the exports needed are already
// baked in there): 1 east, 2 west, 3 south, 4 north, 0 = the export has
// no outside door (farms, tower houses, lamp, well).
int pieceFront(int kind) {
    return village::pieceFrontFacing((village::PieceKind)kind);
}

// Cycle index of a java facing walking CLOCKWISE (north, east, south,
// west): how many quarter turns the template needs so its door ends up
// pointing at `target`.
int facingCycleIndex(uint8_t f) {
    switch (f) {
        case 4: return 0;  // north
        case 1: return 1;  // east
        case 3: return 2;  // south
        case 2: return 3;  // west
        default: return 0;
    }
}
int cwFacingSteps(uint8_t front, uint8_t target) {
    return (facingCycleIndex(target) - facingCycleIndex(front) + 4) & 3;
}

// Weighted pick honouring the one-copy caps. Re-rolls on a capped pick,
// giving up (and skipping the slot) after a few tries — deterministic,
// because the rng stream and the counts are identical in every chunk.
int pickBuilding(GenRandom& rng, int counts[village::PIECE_COUNT]) {
    using namespace village;
    for (int attempt = 0; attempt < 6; ++attempt) {
        int r = (int)rng.nextInt(kPoolTotal);
        int kind = kPool[std::size(kPool) - 1].kind;
        for (const PoolEntry& e : kPool) {
            if (r < e.weight) { kind = e.kind; break; }
            r -= e.weight;
        }
        const bool capped =
            (kind == PIECE_CHURCH || kind == PIECE_LIBRARY ||
             kind == PIECE_BLACKSMITH || kind == PIECE_BUTCHER) &&
            counts[kind] >= 1;
        if (!capped) return kind;
    }
    return -1;
}

void buildLayout(int64_t seed, int regionX, int regionZ,
                 int anchorChunkX, int anchorChunkZ, VillagePlan& out) {
    using namespace village;

    out = VillagePlan{};

    // The layout rng is mixed differently from regionCandidate's so the
    // well offset and the anchor chunk cannot correlate.
    GenRandom rng(seed * 341873128712LL +
                  (int64_t)regionX * 132897987541LL +
                  (int64_t)regionZ * 341873128712LL +
                  kSalt * 2 + 0x5F3759DFLL);

    // Well: inside the anchor chunk, nudged so the roads leave from a
    // spot near its middle instead of a corner.
    const int wellWx = anchorChunkX * 16 + 4 + (int)rng.nextInt(6);
    const int wellWz = anchorChunkZ * 16 + 4 + (int)rng.nextInt(6);
    const int villageRot = (int)rng.nextInt(4);

    int counts[PIECE_COUNT] = {};

    // Village-frame -> world: rotate the box around the village origin
    // and translate it to the well's corner. Total piece rotation =
    // piece rotation + village rotation (quarter turns compose).
    auto place = [&](int kind, int pieceRot, int vx, int vz, int vw, int vh) {
        int wx = vx, wz = vz, w = vw, h = vh;
        rotateRect(wx, wz, w, h, villageRot);
        wx += wellWx; wz += wellWz;
        out.pieces.push_back(
            VillagePiecePlacement{kind, (pieceRot + villageRot) & 3, wx, wz, w, h});
    };
    auto cell = [&](int vx, int vz) {
        int x = vx, z = vz, w = 1, h = 1;
        rotateRect(x, z, w, h, villageRot);
        out.pathCells.emplace_back(x + wellWx, z + wellWz);
    };

    // The well itself, unrotated in the village frame.
    place(PIECE_WELL, 0, 0, 0, 6, 6);

    for (int armIdx = 0; armIdx < 4; ++armIdx) {
        const ArmSpec& arm = kArms[armIdx];

        // Street lamp beside the first stretch of road: 3x3 footprint
        // hugging the road lanes, drawn before the buildings so its slot
        // is reserved.
        {
            const int lampRot = (int)rng.nextInt(4);
            const int a0 = alongStart(arm.dir, 0, 3);
            const int p0 = perpStart(arm.perpSide, 3);
            const int vx = arm.alongX ? a0 : p0;
            const int vz = arm.alongX ? p0 : a0;
            place(PIECE_STREETLIGHT, lampRot, vx, vz, 3, 3);
        }

        // Buildings march down the arm, one side only.
        int t = 4;
        int lineEnd = 6;
        const int n = 1 + (int)rng.nextInt(2);
        for (int bi = 0; bi < n; ++bi) {
            const int kind = pickBuilding(rng, counts);
            if (kind < 0) continue;
            const Structure* s = piece((PieceKind)kind);

            int pieceRot;
            const int front = pieceFront(kind);
            if (front != 0)
                pieceRot = cwFacingSteps((uint8_t)front, (uint8_t)arm.doorTarget);
            else
                pieceRot = (int)rng.nextInt(4);

            const int rw = s->rotatedSizeX(pieceRot);
            const int rh = s->rotatedSizeZ(pieceRot);
            const int alongExt = arm.alongX ? rw : rh;
            const int perpExt  = arm.alongX ? rh : rw;
            const int a0 = alongStart(arm.dir, t, alongExt);
            const int p0 = perpStart(arm.perpSide, perpExt);
            const int vx = arm.alongX ? a0 : p0;
            const int vz = arm.alongX ? p0 : a0;
            place(kind, pieceRot, vx, vz, rw, rh);

            counts[kind]++;
            t += alongExt + 2;
            lineEnd = t - 2;   // far edge of this building
        }

        // The road: a 3-wide pad spilling out of the well face, then two
        // lanes running past the last building.
        for (int tt = 0; tt <= 1; ++tt)
            for (int p = 1; p <= 3; ++p)
                cell(arm.alongX ? alongCoord(arm.dir, tt) : p,
                     arm.alongX ? p : alongCoord(arm.dir, tt));
        for (int tt = 2; tt <= lineEnd; ++tt)
            for (int p = 1; p <= 2; ++p)
                cell(arm.alongX ? alongCoord(arm.dir, tt) : p,
                     arm.alongX ? p : alongCoord(arm.dir, tt));
    }

    // A road never runs through a building: drop every cell that lands
    // inside any piece's footprint (the lamp overlaps the pad corners).
    std::erase_if(out.pathCells, [&](const std::pair<int, int>& c) {
        for (const VillagePiecePlacement& p : out.pieces)
            if (c.first >= p.x && c.first < p.x + p.w &&
                c.second >= p.z && c.second < p.z + p.h)
                return true;
        return false;
    });

    // World bounding box over everything.
    if (!out.pieces.empty()) {
        out.minX = out.pieces[0].x; out.maxX = out.pieces[0].x + out.pieces[0].w;
        out.minZ = out.pieces[0].z; out.maxZ = out.pieces[0].z + out.pieces[0].h;
    }
    for (const VillagePiecePlacement& p : out.pieces) {
        out.minX = std::min(out.minX, p.x);
        out.maxX = std::max(out.maxX, p.x + p.w);
        out.minZ = std::min(out.minZ, p.z);
        out.maxZ = std::max(out.maxZ, p.z + p.h);
    }
    for (const auto& c : out.pathCells) {
        out.minX = std::min(out.minX, c.first);
        out.maxX = std::max(out.maxX, c.first + 1);
        out.minZ = std::min(out.minZ, c.second);
        out.maxZ = std::max(out.maxZ, c.second + 1);
    }
    // Well center for the biome gate.
    const VillagePiecePlacement& well = out.pieces[0];
    out.wellCx = well.x + well.w / 2;
    out.wellCz = well.z + well.h / 2;
}

// ---- Terrain gates ---------------------------------------------------------
//
// Reading one column is expensive (the height field smooths a 3x3 raw
// grid and the biome adds several climate fields — tens of microseconds
// a column), and a village needs hundreds of them. Every chunk that
// touches the village would otherwise re-ask the SAME questions and get
// the SAME answers, so the answers are read once per region and cached
// with the site below; placing a chunk is then pure block writing.

// Terrain facts about one world column, captured while a piece (or a
// road cell) is judged.
struct SiteColumn {
    int16_t x, z;
    int16_t h;        // terrain height: the surface block sits at h-1
    uint8_t ground;   // the biome's surface block (used to pack under pieces)
    uint8_t stamp;    // road cells only: may a road be drawn here?
};

// Same contract as StructurePopulator's structureBaseY, for one piece:
// every column under the (rotated) footprint must be a village biome,
// dry, below the world roof, and flat enough; returns highest + 1 (the
// template's first layer), or -1 to drop the piece. When `columns` is
// given it collects the per-column facts of the footprint on the way, so
// filling the ground under the piece later needs no noise at all.
int pieceBaseY(const TerrainNoise& noise, const Structure& s, int rot,
               int wx, int wz, std::vector<SiteColumn>* columns) {
    const int rw = s.rotatedSizeX(rot);
    const int rh = s.rotatedSizeZ(rot);
    int lowest = 128, highest = -1;
    if (columns) columns->clear();

    for (int j = 0; j < rh; ++j) {
        for (int i = 0; i < rw; ++i) {
            const int x = wx + i, z = wz + j;
            const int h = columnHeight(noise, x, z);
            const uint8_t b = columnBiome(noise, x, z, h);
            if (!isVillageBiome(b)) return -1;
            if (h < kSeaLevel + 1) return -1;                  // no water
            if (h > 127 - s.sizeY() - 1) return -1;            // fits under the top
            if (riverMask(noise, x, z) > 0.05) return -1;      // not in a river
            lowest = std::min(lowest, h);
            highest = std::max(highest, h);
            if (columns)
                columns->push_back({(int16_t)x, (int16_t)z, (int16_t)h,
                                    biome::BiomeRegistry::forId(b).topBlock, 1});
        }
    }
    if (highest < 0 || highest - lowest > kMaxGroundStep) return -1;
    return highest + 1;
}

// Pack the ground back up under a piece (same idea as the temple's
// fillUnder) from the cached column facts — no noise, no registry.
void fillUnderPiece(GenerationContext& ctx, const SiteColumn* cols, size_t count,
                    int baseY) {
    mcp_chunk_t* chunk = ctx.chunk;
    const int bx = ctx.chunk_x * 16, bz = ctx.chunk_z * 16;

    for (size_t k = 0; k < count; ++k) {
        const int x = cols[k].x, z = cols[k].z;
        if (x < bx || x >= bx + 16 || z < bz || z >= bz + 16) continue;
        const int h = cols[k].h;
        if (h >= baseY) continue;
        for (int y = h; y < baseY; ++y)
            mcp_chunk_set_block_full(chunk, x - bx, y, z - bz, cols[k].ground, 0);
    }
}

// Road material follows the village biome, the way vanilla draws desert
// village streets in sandstone and plains streets in path blocks.
void pathMaterial(uint8_t wellBiome, uint8_t& id, uint8_t& data) {
    if (wellBiome == (uint8_t)biome::BIOME_DESERT) {
        id = (uint8_t)BlockId::SANDSTONE;   // smooth sandstone roads
        data = 0;
    } else {
        id = (uint8_t)BlockId::GRASS_PATH;
        data = 0;
    }
}

// Stamp one road column: replace the natural surface with the path
// block and clear the vegetation the surface pass scattered over the
// road. Vanilla only rewrites natural ground (a road gives up under a
// tree trunk), so a tree can punch a gap instead of a floating canopy.
//
// Only the surface test is left: whether a road may be drawn at all
// (height, cliff, river, biome) was decided once with the site.
bool stampPath(GenerationContext& ctx, const SiteColumn& c,
               uint8_t pathId, uint8_t pathData) {
    if (!c.stamp) return false;
    mcp_chunk_t* chunk = ctx.chunk;
    const int bx = ctx.chunk_x * 16, bz = ctx.chunk_z * 16;
    const int x = c.x, z = c.z, h = c.h;
    if (x < bx || x >= bx + 16 || z < bz || z >= bz + 16) return false;

    const int lx = x - bx, lz = z - bz;
    const uint8_t top = mcp_chunk_get_block_id(chunk, lx, h - 1, lz);
    const bool natural = top == (uint8_t)BlockId::GRASS ||
                         top == (uint8_t)BlockId::DIRT ||
                         top == (uint8_t)BlockId::SAND;
    if (!natural) return false;              // trunk, rock, building — leave it

    mcp_chunk_set_block_full(chunk, lx, h - 1, lz, pathId, pathData);

    // Clear one or two blocks of vegetation above the road. Anything
    // else up there (a tree) stays: the road gave up on that column
    // above, so this only ever runs over clear ground.
    for (int y = h; y <= h + 1 && y < 128; ++y) {
        const uint8_t id = mcp_chunk_get_block_id(chunk, lx, y, lz);
        if (id == (uint8_t)BlockId::TALL_GRASS ||
            id == (uint8_t)BlockId::DEAD_BUSH ||
            id == (uint8_t)BlockId::FLOWER_YELLOW ||
            id == (uint8_t)BlockId::FLOWER_RED ||
            id == (uint8_t)BlockId::SAPLING ||
            id == (uint8_t)BlockId::SNOW_LAYER ||
            id == (uint8_t)BlockId::DOUBLE_PLANT)
            mcp_chunk_set_block_full(chunk, lx, y, lz, 0, 0);
    }
    return true;
}

// ---- The cached village site -----------------------------------------------
//
// A whole village answered once: the layout, whether it may generate at
// all, the base height of every piece and the terrain facts of every
// column those pieces and roads touch. Pure function of
// (seed, regionX, regionZ), so every chunk reaches the same verdict —
// it is cached, never recomputed per chunk.
struct VillageSite {
    int64_t seed = 0;
    int regionX = 0, regionZ = 0;
    bool valid = false;
    VillagePlan plan;
    bool stands = false;
    uint8_t wellBiome = 0;
    int wellH = 0;
    std::vector<int> baseY;                   // per piece, -1 = dropped
    std::vector<size_t> columnStart;          // per piece + 1, into columns
    std::vector<SiteColumn> columns;          // piece footprints, row major
    std::vector<SiteColumn> pathColumns;      // road cells with their verdict
};

void buildSite(int64_t seed, int rx, int rz, const TerrainNoise& n,
               VillageSite& s) {
    s.seed = seed; s.regionX = rx; s.regionZ = rz; s.valid = true;
    VillagePopulator::buildPlan(seed, rx, rz, s.plan);
    s.stands = VillagePopulator::villageStands(n, s.plan, s.wellBiome, s.wellH);
    s.baseY.assign(s.plan.pieces.size(), -1);
    s.columnStart.assign(s.plan.pieces.size() + 1, 0);
    s.columns.clear();
    s.pathColumns.clear();
    if (!s.stands) return;   // the whole village is dropped

    // Collected per piece and appended: pieceBaseY clears what it is
    // given, so every piece needs its own scratch buffer.
    std::vector<SiteColumn> pieceCols;
    for (size_t i = 0; i < s.plan.pieces.size(); ++i) {
        s.columnStart[i] = s.columns.size();
        const VillagePiecePlacement& p = s.plan.pieces[i];
        const Structure* st = village::piece((village::PieceKind)p.kind);
        s.baseY[i] = pieceBaseY(n, *st, p.rot, p.x, p.z, &pieceCols);
        if (s.baseY[i] < 0) continue;      // dropped: no columns to keep
        s.columns.insert(s.columns.end(), pieceCols.begin(), pieceCols.end());
    }
    s.columnStart[s.plan.pieces.size()] = s.columns.size();

    for (const auto& cell : s.plan.pathCells) {
        SiteColumn c{};
        c.x = (int16_t)cell.first;
        c.z = (int16_t)cell.second;
        c.h = (int16_t)columnHeight(n, cell.first, cell.second);
        const uint8_t b = columnBiome(n, cell.first, cell.second, c.h);
        c.ground = biome::BiomeRegistry::forId(b).topBlock;
        // The road gives up on a cliff rather than climbing it, and like
        // the pieces it refuses water, rivers and other biomes.
        c.stamp = (c.h >= kSeaLevel + 1 && c.h <= 126 &&
                   c.h - s.wellH <= kMaxPathStep && s.wellH - c.h <= kMaxPathStep &&
                   riverMask(n, cell.first, cell.second) <= 0.05 &&
                   isVillageBiome(b)) ? 1 : 0;
        s.pathColumns.push_back(c);
    }
}

// Few slots are enough: a chunk only ever looks at its own region (the
// anchors of neighbouring regions are hundreds of blocks away) and
// generation walks regions in order. thread_local because chunks are
// generated on worker threads — see the note in CaveCarver.h — so every
// worker keeps its own answers and none of them share a lock.
constexpr int kSiteSlots = 4;
thread_local VillageSite tSites[kSiteSlots];
thread_local uint64_t tSiteUsed[kSiteSlots] = {};
thread_local uint64_t tSiteTick = 0;

const VillageSite& cachedSite(int64_t seed, int rx, int rz,
                              const TerrainNoise& n) {
    for (int i = 0; i < kSiteSlots; ++i)
        if (tSites[i].valid && tSites[i].seed == seed &&
            tSites[i].regionX == rx && tSites[i].regionZ == rz) {
            tSiteUsed[i] = ++tSiteTick;
            return tSites[i];
        }

    int slot = 0;                       // least recently used
    for (int i = 1; i < kSiteSlots; ++i)
        if (tSiteUsed[i] < tSiteUsed[slot]) slot = i;
    buildSite(seed, rx, rz, n, tSites[slot]);
    tSiteUsed[slot] = ++tSiteTick;
    return tSites[slot];
}

} // namespace

void VillagePopulator::buildPlan(int64_t seed, int regionX, int regionZ,
                                 VillagePlan& out) {
    int anchorChunkX, anchorChunkZ;
    regionCandidate(seed, regionX, regionZ, kSalt, anchorChunkX, anchorChunkZ);
    buildLayout(seed, regionX, regionZ, anchorChunkX, anchorChunkZ, out);
}

uint64_t& VillagePopulator::nanosAccumulator() {
    static thread_local uint64_t nanos = 0;
    return nanos;
}

bool VillagePopulator::villageStands(const TerrainNoise& noise,
                                     const VillagePlan& plan,
                                     uint8_t& outBiome, int& outWellH) {
    outWellH = columnHeight(noise, plan.wellCx, plan.wellCz);
    outBiome = columnBiome(noise, plan.wellCx, plan.wellCz, outWellH);
    if (!isVillageBiome(outBiome)) return false;
    // The well is the start piece: when it cannot stand (lake, river,
    // cliff), the whole village is dropped instead of leaving roads
    // crawling toward nothing.
    if (plan.pieces.empty()) return false;
    const VillagePiecePlacement& well = plan.pieces[0];
    const Structure* wellS = village::piece(village::PIECE_WELL);
    return pieceBaseY(noise, *wellS, well.rot, well.x, well.z, nullptr) >= 0;
}

void VillagePopulator::populate(GenerationContext& ctx) {
    const auto started = std::chrono::steady_clock::now();
    mcp_chunk_t* chunk = ctx.chunk;
    const int bx = ctx.chunk_x * 16, bz = ctx.chunk_z * 16;
    const int regionX = (int)std::floor((double)ctx.chunk_x / kSpacing);
    const int regionZ = (int)std::floor((double)ctx.chunk_z / kSpacing);

    // Terrain is built lazily: the overwhelming majority of chunks are
    // not near any village anchor, and constructing the five simplex
    // layers for them would be wasted work on every chunk of the world.
    std::optional<TerrainNoise> noise;
    bool dirty = false;

    for (int rx = regionX - 1; rx <= regionX + 1; ++rx) {
        for (int rz = regionZ - 1; rz <= regionZ + 1; ++rz) {
            int anchorChunkX, anchorChunkZ;
            regionCandidate(ctx.seed, rx, rz, kSalt, anchorChunkX, anchorChunkZ);

            // Loose reject before anything is built: anchors sit at least
            // 17 chunks inside their region, so only the chunk's own
            // region can ever come near it.
            const int awx = anchorChunkX * 16, awz = anchorChunkZ * 16;
            if (bx > awx + kVillageRadius || bx + 16 < awx - kVillageRadius ||
                bz > awz + kVillageRadius || bz + 16 < awz - kVillageRadius)
                continue;

            if (!noise.has_value()) noise.emplace(ctx.seed);
            const VillageSite& site = cachedSite(ctx.seed, rx, rz, *noise);
            const VillagePlan& plan = site.plan;

            if (bx >= plan.maxX || bx + 16 <= plan.minX ||
                bz >= plan.maxZ || bz + 16 <= plan.minZ)
                continue;
            if (!site.stands) continue;

            uint8_t pathId, pathData;
            pathMaterial(site.wellBiome, pathId, pathData);
            for (const SiteColumn& c : site.pathColumns)
                dirty |= stampPath(ctx, c, pathId, pathData);

            // The resolver reads the village biome through this pointer;
            // it never writes it.
            uint8_t wellBiome = site.wellBiome;

            for (size_t i = 0; i < plan.pieces.size(); ++i) {
                const VillagePiecePlacement& p = plan.pieces[i];
                // A piece that cannot reach this chunk costs nothing.
                if (p.x >= bx + 16 || p.x + p.w <= bx ||
                    p.z >= bz + 16 || p.z + p.h <= bz)
                    continue;
                const int baseY = site.baseY[i];
                if (baseY < 0) continue;
                fillUnderPiece(ctx, site.columns.data() + site.columnStart[i],
                               site.columnStart[i + 1] - site.columnStart[i],
                               baseY);
                const Structure* s = village::piece((village::PieceKind)p.kind);
                const size_t wrote = s->placeInto(
                    chunk, ctx.chunk_x, ctx.chunk_z, p.x, baseY, p.z,
                    (cursestone::level::generator::structure::Rotation)p.rot,
                    village::resolve, &wellBiome);
                if (wrote > 0) dirty = true;
            }
        }
    }

    if (dirty) chunk->dirty = true;

    nanosAccumulator() += (uint64_t)std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started)
        .count();
}

} // namespace cursestone::level::generator::populator
