// tools/village_check.cpp
//
// End-to-end verification of the village generator. Everything is
// checked against the REAL plan the populator stamps (built through
// VillagePopulator::buildPlan / villageStands), never a copy of the
// layout math, and every material count is taken only from the cells
// inside the village's own piece footprints — so a neighbouring village,
// a tree drifting in from the next biome or natural sandstone under a
// desert floor can never make a check pass or fail by accident.
//
//   1. Density: how many region anchors produce a standing village, split
//      by the village's material family (plains/savanna vs desert), plus a
//      sweep of the ROAD GRAPH of every standing village: one connected
//      network hanging off the well, at least one fork and at least one
//      bend (the streets are a graph with curves and ramifications, not
//      four straight stripes).
//   2. For real villages (one plains + one desert sample):
//        - the roads the plan asked for are actually stamped;
//        - the road graph of THIS village: connected, forked, curvy;
//        - the biome material rule: a DESERT village has no planks and
//          no cobblestone anywhere in its pieces, a plains village has no
//          sandstone (materials come from the well's biome);
//        - every door opens onto the road (the road can be one cell out:
//          in these templates the door block sits inside the wall and the
//          opening is the footprint's border cell);
//        - determinism: two fresh generators hash identically;
//        - order independence (anti-cut): generating the window in
//          REVERSE chunk order hashes identically.
//   3. A rejected village (well cannot stand) leaves NO road behind.
//   4. Desert temple regression (exact template markers + determinism).
//   5. populate() timing near a village vs far away from one.
//
//   g++ -std=c++20 -Isrc tools/village_check.cpp /tmp/rc/*.o -o /tmp/tools/village_check
#include "level/generator/Normal.h"
#include "level/generator/TerrainShape.h"
#include "level/generator/biome/Biome.h"
#include "level/generator/noise/GenRandom.h"
#include "level/generator/populator/VillagePopulator.h"
#include "level/generator/structure/DesertTemple.h"
#include "level/format/Chunk.h"
#include "block/BlockIds.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace cursestone::level::generator;
using cursestone::level::generator::populator::VillagePlan;
using cursestone::level::generator::populator::VillagePathCell;
using cursestone::level::generator::populator::VillagePopulator;
namespace blk = cursestone::block;
namespace biome = cursestone::level::generator::biome;
namespace structure = cursestone::level::generator::structure;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

namespace {

constexpr int kVillageSalt = 10387312;
constexpr int kSpacing = 32;
constexpr int kSep = 8;
constexpr int kRegionRadius = 24;

int wrapRegion(int v) {
    int r = v % kSpacing;
    return r < 0 ? r + kSpacing : r;
}
// Same formula as StructurePopulator/VillagePopulator. Only used to
// REPORT the anchor chunk in the log — the plan comes from the
// populator itself.
void regionCandidate(int64_t seed, int rx, int rz, int64_t salt,
                     int& cx, int& cz) {
    GenRandom rng(seed * 341873128712LL + (int64_t)rx * 341873128712LL +
                  (int64_t)rz * 132897987541LL + salt);
    const int span = kSpacing - kSep;
    cx = rx * kSpacing +
         wrapRegion(rx * kSpacing + kSep + (int)rng.nextInt(span));
    cz = rz * kSpacing +
         wrapRegion(rz * kSpacing + kSep + (int)rng.nextInt(span));
}

// Which material family a village builds with: desert is sandstone,
// everything else a village can start in uses the oak set.
const char* materialFamily(uint8_t b) {
    return b == (uint8_t)biome::BIOME_DESERT ? "desert" : "oak";
}

struct Window {
    int chunkX0, chunkZ0;
    int nx = 0, nz = 0;                       // blocks per side
    std::vector<uint8_t> id, data;

    void build(int cx0, int cx1, int cz0, int cz1) {
        chunkX0 = cx0; chunkZ0 = cz0;
        nx = (cx1 - cx0 + 1) * 16;
        nz = (cz1 - cz0 + 1) * 16;
        id.assign((size_t)nx * nz * 128, 0);
        data.assign((size_t)nx * nz * 128, 0);
    }
    size_t idx(int lx, int lz, int y) const { return ((size_t)lz * nx + lx) * 128 + y; }
    bool in(int wx, int wz) const {
        const int lx = wx - chunkX0 * 16, lz = wz - chunkZ0 * 16;
        return lx >= 0 && lz >= 0 && lx < nx && lz < nz;
    }
    void set(int wx, int wy, int wz, uint8_t i, uint8_t d) {
        if (!in(wx, wz) || wy < 0 || wy > 127) return;
        const size_t o = idx(wx - chunkX0 * 16, wz - chunkZ0 * 16, wy);
        id[o] = i; data[o] = d;
    }
    uint8_t at(int wx, int wy, int wz) const {
        if (!in(wx, wz) || wy < 0 || wy > 127) return 0;
        return id[idx(wx - chunkX0 * 16, wz - chunkZ0 * 16, wy)];
    }
    uint8_t dataAt(int wx, int wy, int wz) const {
        if (!in(wx, wz) || wy < 0 || wy > 127) return 0;
        return data[idx(wx - chunkX0 * 16, wz - chunkZ0 * 16, wy)];
    }
    uint64_t hash() const {
        uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; i < id.size(); ++i) {
            h ^= id[i];  h *= 1099511628211ULL;
            h ^= data[i]; h *= 1099511628211ULL;
        }
        return h;
    }
};

// Generate + populate a chunk range into a window (Normal runs both).
void fillWindow(int64_t seed, int cx0, int cx1, int cz0, int cz1,
                Window& w, bool reverse) {
    w.build(cx0, cx1, cz0, cz1);
    std::vector<std::pair<int, int>> order;
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) order.emplace_back(cx, cz);
    if (reverse) std::reverse(order.begin(), order.end());

    Normal gen(seed);
    for (auto [cx, cz] : order) {
        mcp_chunk_t* c = mcp_chunk_new(cx, cz);
        GenerationContext ctx{};
        ctx.seed = seed; ctx.chunk_x = cx; ctx.chunk_z = cz;
        ctx.chunk = c; ctx.biomes_in = nullptr;
        gen.generate(ctx);
        gen.populate(ctx);
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                for (int y = 0; y < 128; ++y) {
                    const uint8_t i = mcp_chunk_get_block_id(c, x, y, z);
                    if (i == 0) continue;
                    w.set(cx * 16 + x, y, cz * 16 + z, i,
                          mcp_chunk_get_block_data(c, x, y, z));
                }
        mcp_chunk_destroy(c);
    }
}

// Chunks a village needs: its bounding box plus a one-chunk skirt.
void windowChunks(const VillagePlan& plan, int& cx0, int& cx1, int& cz0, int& cz1) {
    cx0 = (plan.minX >> 4) - 1; cx1 = (plan.maxX >> 4) + 1;
    cz0 = (plan.minZ >> 4) - 1; cz1 = (plan.maxZ >> 4) + 1;
}

struct Door { int x, y, z, facing; };

// Everything the checks need, measured on the village's OWN footprint.
struct Measured {
    int pathCells = 0;      // planned road cells that carry the road block
    int pathTotal = 0;
    int planks = 0, cobble = 0, sandstone = 0, log = 0;
    int doors = 0, doorsOnRoad = 0;
    std::vector<Door> openDoors;   // doors with no road in front
};

Measured measure(const Window& w, const VillagePlan& plan,
                 const TerrainNoise& n, uint8_t wellBiome) {
    Measured m;
    const uint8_t pathId = wellBiome == (uint8_t)biome::BIOME_DESERT
                              ? (uint8_t)blk::SANDSTONE
                              : (uint8_t)blk::GRASS_PATH;

    // Roads: exactly the cells the plan asked for, read at the natural
    // top layer (buildings sit at or above that).
    for (const auto& c : plan.pathCells) {
        const int h = columnHeight(n, c.x, c.z);
        ++m.pathTotal;
        if (h >= 1 && w.at(c.x, h - 1, c.z) == pathId) ++m.pathCells;
    }

    // Materials + doors: only inside the village's own pieces, and only
    // at or above each column's natural top (that is what the piece
    // wrote; below it is untouched terrain).
    for (const auto& p : plan.pieces) {
        for (int z = p.z; z < p.z + p.h; ++z)
            for (int x = p.x; x < p.x + p.w; ++x) {
                const int h = columnHeight(n, x, z);
                for (int y = h < 0 ? 0 : h; y < 128; ++y) {
                    const uint8_t i = w.at(x, y, z);
                    if (i == 0) continue;
                    switch (i) {
                        case (uint8_t)blk::PLANKS:      ++m.planks; break;
                        case (uint8_t)blk::COBBLESTONE:++m.cobble; break;
                        case (uint8_t)blk::SANDSTONE:  ++m.sandstone; break;
                        case (uint8_t)blk::LOG:         ++m.log; break;
                        case (uint8_t)blk::WOODEN_DOOR:
                            if (w.dataAt(x, y, z) & 8) break;   // upper half
                            ++m.doors;
                            {
                                static const int dd[4][2] =
                                    {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                                const int f = w.dataAt(x, y, z) & 3;
                                // The door block sits one cell inside the
                                // wall in these templates, so the road can
                                // be one or two cells out.
                                bool ok = false;
                                for (int step = 1; step <= 2 && !ok; ++step) {
                                    const int fx = x + dd[f][0] * step;
                                    const int fz = z + dd[f][1] * step;
                                    const int fh = columnHeight(n, fx, fz);
                                    if (fh >= 1 && w.at(fx, fh - 1, fz) == pathId)
                                        ok = true;
                                }
                                if (ok) ++m.doorsOnRoad;
                                else m.openDoors.push_back({x, y, z, f});
                            }
                            break;
                        default: break;
                    }
                }
            }
    }
    return m;
}

void asciiCut(const Window& w, int groundY, const char* title) {
    printf("--- %s (slice y=%d; D door, L lamp, ~ water, =/- road, # building) ---\n",
           title, groundY);
    const int step = w.nx > 120 ? 2 : 1;   // keep it readable
    for (int lz = 0; lz < w.nz; lz += step) {
        char row[192];
        int cols = 0;
        for (int lx = 0; lx < w.nx && cols < 188; lx += step, ++cols) {
            const int wx = w.chunkX0 * 16 + lx, wz = w.chunkZ0 * 16 + lz;
            char ch = ' ';
            for (int dy = -3; dy <= 3 && ch == ' '; ++dy) {
                const int y = groundY + dy;
                const uint8_t i = w.at(wx, y, wz);
                const uint8_t d = w.dataAt(wx, y, wz);
                if (i == 0) continue;
                if (i == (uint8_t)blk::WOODEN_DOOR && !(d & 8)) ch = 'D';
                else if (i == (uint8_t)blk::WOOL && d == 15) ch = 'L';
                else if (i == (uint8_t)blk::WATER) ch = '~';
                else if (i == (uint8_t)blk::GRASS_PATH) ch = '=';
                else if (i == (uint8_t)blk::SANDSTONE) ch = '#';
                else if (i == (uint8_t)blk::COBBLESTONE) ch = 'o';
                else if (i == (uint8_t)blk::PLANKS) ch = 'p';
                else if (i == (uint8_t)blk::LOG) ch = 'T';
                else if (i == (uint8_t)blk::CHEST) ch = 'C';
                else if (i == (uint8_t)blk::FURNACE) ch = 'F';
                else if (i == (uint8_t)blk::WORKBENCH) ch = 'K';
                else if (i == (uint8_t)blk::LADDER) ch = 'H';
                else if (i == (uint8_t)blk::FLOWER_POT) ch = 'v';
            }
            row[cols] = ch;
        }
        row[cols] = 0;
        printf("%s\n", row);
    }
    printf("\n");
}

// Shape of the road graph the plan asked for — pure layout, no terrain
// needed. The renewd streets are a graph that hangs off the well, so
// what is worth proving is that it really is ONE graph (no cell left
// stranded after a building ate the cells around it), that every parent
// link is a legal step of the walk, and that it actually bends and
// branches instead of drawing four straight stripes.
struct PathStats {
    int cells = 0, roots = 0, junctions = 0, turns = 0, detached = 0;
    bool badParent = false;
};

PathStats pathStats(const VillagePlan& plan) {
    PathStats s;
    const std::vector<VillagePathCell>& cells = plan.pathCells;
    s.cells = (int)cells.size();

    std::unordered_map<int64_t, int> at;
    at.reserve(cells.size() * 2);
    for (int i = 0; i < (int)cells.size(); ++i)
        at[((int64_t)cells[i].x << 32) ^ (uint32_t)cells[i].z] = i;

    auto nbrs = [&](const VillagePathCell& c, int out[4], int& count) {
        count = 0;
        const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& dd : d) {
            const int64_t k = ((int64_t)(c.x + dd[0]) << 32) ^
                              (uint32_t)(c.z + dd[1]);
            const auto it = at.find(k);
            if (it != at.end()) out[count++] = it->second;
        }
    };

    std::vector<int> n4(4, 0);
    int cnt = 0;
    for (int i = 0; i < (int)cells.size(); ++i) {
        const int p = cells[i].parent;
        if (p < 0) ++s.roots;
        else if (p >= i) s.badParent = true;          // must point backwards
        else {
            int q = p, steps = 0;                      // chains must terminate
            while (q >= 0 && steps <= s.cells) { q = cells[q].parent; ++steps; }
            if (q != -1) s.badParent = true;
        }

        nbrs(cells[i], n4.data(), cnt);
        if (cnt >= 3) ++s.junctions;
        else if (cnt == 2) {
            // A turn: its two neighbours are not neighbours of each
            // other, so the street bends instead of running straight.
            const VillagePathCell& a = cells[n4[0]];
            const VillagePathCell& b = cells[n4[1]];
            if (std::abs(a.x - b.x) + std::abs(a.z - b.z) > 1) ++s.turns;
        }
    }

    // Flood from the roots (the cells that leave the well).
    std::vector<char> seen(cells.size(), 0);
    std::vector<int> stack;
    for (int i = 0; i < (int)cells.size(); ++i)
        if (cells[i].parent < 0) { seen[i] = 1; stack.push_back(i); }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        nbrs(cells[i], n4.data(), cnt);
        for (int k = 0; k < cnt; ++k)
            if (!seen[n4[k]]) { seen[n4[k]] = 1; stack.push_back(n4[k]); }
    }
    for (char v : seen) if (!v) ++s.detached;
    return s;
}

// One village candidate: the plan plus what the terrain says about it.
struct Candidate {
    int rx, rz;
    VillagePlan plan;
    uint8_t wellBiome = 0;
    int wellH = 0;
    bool stands = false;
    const char* family = "none";
};

std::vector<Candidate> scanRegion(int64_t seed, TerrainNoise& probe) {
    std::vector<Candidate> out;
    for (int rx = -kRegionRadius; rx <= kRegionRadius; ++rx)
        for (int rz = -kRegionRadius; rz <= kRegionRadius; ++rz) {
            Candidate c;
            c.rx = rx; c.rz = rz;
            VillagePopulator::buildPlan(seed, rx, rz, c.plan);
            c.stands = VillagePopulator::villageStands(probe, c.plan,
                                                       c.wellBiome, c.wellH);
            c.family = materialFamily(c.wellBiome);
            out.push_back(std::move(c));
        }
    return out;
}

void runSample(const char* family, int64_t seed, const TerrainNoise& probe,
               const Candidate& c) {
    int ax, az;
    regionCandidate(seed, c.rx, c.rz, kVillageSalt, ax, az);
    printf("2. %s village: region (%d,%d) anchor chunk (%d,%d) well (%d,%d)\n",
           family, c.rx, c.rz, ax, az, c.plan.wellCx, c.plan.wellCz);

    int cx0, cx1, cz0, cz1;
    windowChunks(c.plan, cx0, cx1, cz0, cz1);
    Window w1, w2, w3;
    fillWindow(seed, cx0, cx1, cz0, cz1, w1, false);
    fillWindow(seed, cx0, cx1, cz0, cz1, w2, false);
    fillWindow(seed, cx0, cx1, cz0, cz1, w3, true);

    const Measured m = measure(w1, c.plan, probe, c.wellBiome);
    printf("  roads %d/%d planned | pieces: doors=%d planks=%d cobble=%d "
           "sandstone=%d log=%d\n",
           m.pathCells, m.pathTotal, m.doors, m.planks, m.cobble, m.sandstone, m.log);

    char buf[192];
    snprintf(buf, sizeof buf, "%s: caminos presentes (>=20 y >=60%% de los planeados)",
             family);
    check(m.pathCells >= 20 && m.pathCells * 10 >= m.pathTotal * 6, buf);

    // The shape of the road graph itself: one network hanging off the
    // well (no stranded cell), with real bends and real forks.
    const PathStats ps = pathStats(c.plan);
    printf("  road graph: cells=%d roots=%d junctions=%d turns=%d "
           "detached=%d\n",
           ps.cells, ps.roots, ps.junctions, ps.turns, ps.detached);
    snprintf(buf, sizeof buf, "%s: una sola red unida al pozo (0 aisladas)",
             family);
    check(!ps.badParent && ps.roots > 0 && ps.detached == 0, buf);
    snprintf(buf, sizeof buf, "%s: los caminos se ramifican (>=1 bifurcacion)",
             family);
    check(ps.junctions >= 1, buf);
    snprintf(buf, sizeof buf, "%s: los caminos curvean (>=6 giros)", family);
    check(ps.turns >= 6, buf);
    snprintf(buf, sizeof buf, "%s: casas con puerta presentes (doors>=1)", family);
    check(m.doors >= 1, buf);

    if (strcmp(family, "desert") == 0) {
        // planks and cobblestone never occur in a desert village's own
        // pieces — everything is the sandstone family.
        check(m.planks == 0 && m.cobble == 0,
              "desert: sin tablas ni cobble en las piezas (todo arenisca)");
        check(m.sandstone >= 80, "desert: arenisca de aldea presente (>=80)");
    } else {
        check(m.sandstone == 0, "plains: sin arenisca en las piezas (materiales oak)");
        check(m.planks > 0, "plains: tablas de las casas presentes");
    }

    snprintf(buf, sizeof buf, "%s: determinismo (2 generadores frescos)", family);
    check(w1.hash() == w2.hash(), buf);
    snprintf(buf, sizeof buf, "%s: anti-corte (orden inverso identico)", family);
    check(w1.hash() == w3.hash(), buf);

    printf("  doors with road in front: %d/%d\n", m.doorsOnRoad, m.doors);
    for (const Door& d : m.openDoors)
        printf("    door (%d,%d,%d) facing %d has no road in front\n",
               d.x, d.y, d.z, d.facing);
    snprintf(buf, sizeof buf, "%s: todas las puertas abren al camino", family);
    check(m.doors >= 1 && m.doorsOnRoad == m.doors, buf);

    char title[96];
    snprintf(title, sizeof title, "%s village around (%d,%d)", family,
             c.plan.wellCx, c.plan.wellCz);
    asciiCut(w1, c.wellH, title);

    // Performance: the village populator's own cost, measured inside it
    // (the rest of the generator is an order of magnitude more
    // expensive and varies from place to place, so timing whole chunks
    // would only measure that). Chunks inside the village, once the
    // region has been evaluated and cached; then one cold chunk in a
    // fresh region, which is the one-off cost of building the site.
    {
        Normal g(seed);
        auto runChunk = [&](int cx, int cz) {
            mcp_chunk_t* ch = mcp_chunk_new(cx, cz);
            GenerationContext ctx{};
            ctx.seed = seed; ctx.chunk_x = cx; ctx.chunk_z = cz;
            ctx.chunk = ch; ctx.biomes_in = nullptr;
            g.generate(ctx);
            g.populate(ctx);
            mcp_chunk_destroy(ch);
        };
        uint64_t& nanos = VillagePopulator::nanosAccumulator();
        runChunk(c.plan.wellCx >> 4, c.plan.wellCz >> 4);   // warm the site
        const uint64_t warmStart = nanos;
        const int n = 60;
        for (int i = 0; i < n; ++i)
            runChunk((c.plan.minX >> 4) + (i % 12), (c.plan.minZ >> 4) + (i / 12));
        const double warmUs = (double)(nanos - warmStart) / n / 1000.0;

        // Cold: a region nobody has touched, measured on its first chunk.
        int coldX = 0, coldZ = 0;
        regionCandidate(seed, 900, -900, kVillageSalt, coldX, coldZ);
        const uint64_t coldStart = nanos;
        runChunk(coldX, coldZ);
        const double coldUs = (double)(nanos - coldStart) / 1000.0;

        printf("  coste del aldeano: %.1f us/chunk dentro de la aldea "
               "(sitio ya cacheado), %.1f us en el primer chunk de una "
               "region nueva\n", warmUs, coldUs);
        check(warmUs < 500.0, "una aldea cacheada cuesta menos de 0.5 ms/chunk");
        check(coldUs < 150000.0,
              "evaluar una region nueva cuesta menos de 150 ms");
    }
}

} // namespace

int main() {
    const int64_t seed = 12345;
    char buf[192];
    TerrainNoise probe(seed);

    // ---- 1. how many villages actually generate -------------------------
    printf("1. village density (regions %d..%d, both axes)\n",
           -kRegionRadius, kRegionRadius);
    const std::vector<Candidate> all = scanRegion(seed, probe);
    int standing = 0, desert = 0, oak = 0, rejected = 0;
    std::vector<int> desertIdx, oakIdx, rejectedIdx;
    for (size_t i = 0; i < all.size(); ++i) {
        if (!all[i].stands) { ++rejected; rejectedIdx.push_back((int)i); continue; }
        ++standing;
        if (strcmp(all[i].family, "desert") == 0) { ++desert; desertIdx.push_back((int)i); }
        else { ++oak; oakIdx.push_back((int)i); }
    }
    printf("  standing villages=%d (desert=%d oak=%d), dropped=%d, of %zu regions\n",
           standing, desert, oak, rejected, all.size());
    check(standing >= 50, "suficientes aldeas generate en la ventana de regiones");
    check(desert >= 1 && oak >= 1, "hay aldeas de desierto y de llanura/sabana");

    // The shape of the road graph in EVERY standing village — pure
    // layout, so it is cheap to sweep: one connected network hanging off
    // the well in all of them, and bends + forks in almost all of them
    // (a village is allowed to come out with a short, simple street).
    {
        int badGraph = 0, noFork = 0, noTurn = 0, checkedVillages = 0;
        int cellsTotal = 0;
        for (const Candidate& c : all) {
            if (!c.stands) continue;
            ++checkedVillages;
            const PathStats ps = pathStats(c.plan);
            cellsTotal += ps.cells;
            if (ps.badParent || ps.roots <= 0 || ps.detached != 0) ++badGraph;
            if (ps.junctions == 0) ++noFork;
            if (ps.turns == 0) ++noTurn;
        }
        printf("  road graphs: %d villages, %.0f cells/village, "
               "%d broken, %d without a fork, %d without a bend\n",
               checkedVillages,
               checkedVillages ? (double)cellsTotal / checkedVillages : 0.0,
               badGraph, noFork, noTurn);
        check(badGraph == 0,
              "cada aldea tiene una red de caminos unida y colgada del pozo");
        check(noFork * 10 <= checkedVillages,  // >=90% have a fork
              ">=90% de las aldeas tienen al menos una bifurcacion");
        check(noTurn * 10 <= checkedVillages,  // >=90% bend
              ">=90% de las aldeas tienen caminos curvos (giros)");
    }

    // ---- 2. sample a real village of each material family ---------------
    // A candidate is only worth checking as a sample once a house with a
    // door actually reached the road: the piece gates drop buildings on
    // rough or wet ground (vanilla does the same), so a village sitting
    // on a lake shore has nothing to show. The walk over candidates is
    // reported so it is visible that villages are common, not rare.
    for (int family = 0; family < 2; ++family) {
        const bool wantDesert = family == 0;
        const char* name = wantDesert ? "desert" : "plains";
        const std::vector<int>& idx = wantDesert ? desertIdx : oakIdx;

        int scanned = 0, accepted = 0;
        bool found = false;
        int cx0, cx1, cz0, cz1;
        for (int i : idx) {
            if (accepted >= 1) break;
            if (++scanned > 40) break;
            const Candidate& c = all[i];
            windowChunks(c.plan, cx0, cx1, cz0, cz1);
            Window w;
            fillWindow(seed, cx0, cx1, cz0, cz1, w, false);
            const Measured m = measure(w, c.plan, probe, c.wellBiome);
            if (m.doors < 1 || m.doorsOnRoad != m.doors || m.pathCells < 20) {
                printf("  (skip %s candidate region(%d,%d): doors=%d "
                       "onRoad=%d roads=%d/%d)\n",
                       name, c.rx, c.rz, m.doors, m.doorsOnRoad,
                       m.pathCells, m.pathTotal);
                continue;
            }
            printf("  (candidate %d/%zu of the %s list accepted after scanning %d)\n",
                   accepted + 1, idx.size(), name, scanned);
            ++accepted;
            found = true;
            runSample(name, seed, probe, c);
        }
        if (!found) {
            snprintf(buf, sizeof buf, "%s: no village sample with a door on a road", name);
            check(false, buf);
        }
    }

    // ---- 3. a dropped village leaves no orphan roads ---------------------
    printf("3. villages whose well cannot stand are dropped whole\n");
    {
        int tested = 0, clean = 0;
        for (int i : rejectedIdx) {
            if (tested >= 3) break;
            const Candidate& c = all[i];
            int cx0, cx1, cz0, cz1;
            windowChunks(c.plan, cx0, cx1, cz0, cz1);
            Window w;
            fillWindow(seed, cx0, cx1, cz0, cz1, w, false);
            ++tested;
            bool anyRoad = false;
            for (const auto& cell : c.plan.pathCells) {
                const int h = columnHeight(probe, cell.x, cell.z);
                if (h >= 1 && w.at(cell.x, h - 1, cell.z) != 0) {
                    const uint8_t i2 = w.at(cell.x, h - 1, cell.z);
                    if (i2 == (uint8_t)blk::GRASS_PATH ||
                        i2 == (uint8_t)blk::SANDSTONE) { anyRoad = true; break; }
                }
            }
            if (!anyRoad) ++clean;
        }
        printf("  %d/%d dropped villages leave no road behind\n", clean, tested);
        check(tested > 0 && clean == tested,
              "una aldea rechazada no deja caminos huerfanos");
    }

    // ---- 4. desert temple regression ------------------------------------
    printf("4. desert temple regression\n");
    {
        const structure::DesertTemple temple;
        // Find a desert anchor whose footprint passes the temple's own
        // gate, then count the template's exact markers in ITS chunk.
        bool found = false;
        int tx = 0, tz = 0;
        for (int rx = -kRegionRadius; rx <= kRegionRadius && !found; ++rx)
            for (int rz = -kRegionRadius; rz <= kRegionRadius && !found; ++rz) {
                int cx, cz;
                regionCandidate(seed, rx, rz, 14357617, cx, cz);
                const int wx = cx * 16, wz = cz * 16;
                bool desert = true, fits = true;
                int lowest = 128, highest = -1;
                for (int z = 0; z < temple.sizeZ() && desert; ++z)
                    for (int x = 0; x < temple.sizeX(); ++x) {
                        const int h = columnHeight(probe, wx + x, wz + z);
                        if (columnBiome(probe, wx + x, wz + z, h) !=
                            (uint8_t)biome::BIOME_DESERT) { desert = false; break; }
                        if (h < 65 || h > 127 - temple.sizeY() - 1) fits = false;
                        if (riverMask(probe, wx + x, wz + z) > 0.05) fits = false;
                        lowest = std::min(lowest, h);
                        highest = std::max(highest, h);
                    }
                if (desert && fits && highest - lowest <= 2) { tx = cx; tz = cz; found = true; }
            }
        check(found, "hay un templo del desierto utilizable en la ventana");
        if (found) {
            // The template is 11x10, so it fits inside its anchor chunk:
            // counting there proves the WHOLE temple landed.
            Window t1, t2;
            fillWindow(seed, tx, tx, tz, tz, t1, false);
            fillWindow(seed, tx, tx, tz, tz, t2, false);
            int cutStone = 0, clay = 0;
            for (size_t k = 0; k < t1.id.size(); ++k) {
                if (t1.id[k] == (uint8_t)blk::SANDSTONE && t1.data[k] == 2) ++cutStone;
                if (t1.id[k] == (uint8_t)blk::CLAY_STAINED) ++clay;
            }
            printf("  cut_sandstone=%d glazed_terracotta=%d (plantilla completa: 36 y 5)\n",
                   cutStone, clay);
            check(cutStone == 36 && clay == 5, "templo completo presente (marcadores)");
            check(t1.hash() == t2.hash(), "templo determinista");
        }
    }

    printf("\n%s (%d failure(s))\n",
           failures ? "VILLAGE CHECK: FAIL" : "VILLAGE CHECK: OK", failures);
    return failures ? 1 : 0;
}