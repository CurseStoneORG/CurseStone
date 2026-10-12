// SPDX-License-Identifier: LGPL-3.0-or-later
#include "StructurePopulator.h"
#include "world/generator/TerrainShape.h"
#include "world/generator/biome/Biome.h"
#include "world/generator/biome/BiomeRegistry.h"
#include "world/generator/noise/MersenneRandom.h"
#include "block/BlockIds.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cursestone::world::generator::populator {

namespace {

using namespace cursestone::world::generator::structure;

// ---- Vanilla placement constants ----------------------------------------
//
// These are the numbers vanilla uses for the desert pyramid: a candidate
// every 32 x 32 chunks, never closer than 8 chunks to the region edge,
// and salt 14357617. Region-based placement (rather than a per-chunk
// dice roll) is what guarantees the two things the request asked for: no
// two temples ever crowd each other, and a temple can never land on a
// border and get chopped in half — the anchor is a whole chunk.
constexpr int kSpacing = 32;
constexpr int kSeparation = 8;
constexpr int64_t kSalt = 14357617;

// How uneven the ground may be under a structure's footprint. Vanilla
// bakes a template into whatever ground it finds, but a temple hanging
// over a 3-block drop reads as broken, so the flatness test keeps it on
// genuinely flat desert.
constexpr int kMaxGroundStep = 2;

// Positive modulo, so a candidate offset stays correct for the negative
// region coordinates west of the origin.
int wrapRegion(int value) {
    int v = value % kSpacing;
    return v < 0 ? v + kSpacing : v;
}

// The chunk a region would put its structure in, plus which region that
// chunk belongs to. Pure function of (seed, regionX, regionZ).
void regionCandidate(int64_t seed, int regionX, int regionZ, int salt,
                     int& chunkX, int& chunkZ) {
    MersenneRandom rng(seed * 341873128712LL +
                  (int64_t)regionX * 341873128712LL +
                  (int64_t)regionZ * 132897987541LL +
                  (int64_t)salt);
    const int span = kSpacing - kSeparation;
    chunkX = regionX * kSpacing +
             wrapRegion(regionX * kSpacing + kSeparation + (int)rng.nextInt(span));
    chunkZ = regionZ * kSpacing +
             wrapRegion(regionZ * kSpacing + kSeparation + (int)rng.nextInt(span));
}

// The Y the structure's local (0,0,0) cell goes on, or -1 when the spot
// is not usable at all.
//
// Every column under the footprint has to be in one of the structure's
// biomes (vanilla gates placement on a biome list too), dry, high enough
// that the whole template fits under y=127, and flat enough. Returns the
// highest ground under the footprint plus one, which is what keeps the
// template from burying itself in the sand.
int structureBaseY(const TerrainNoise& noise, const Structure& s, int wx, int wz) {
    int lowest = 128;
    int highest = -1;

    for (int lz = 0; lz < s.sizeZ(); ++lz) {
        for (int lx = 0; lx < s.sizeX(); ++lx) {
            const int x = wx + lx;
            const int z = wz + lz;
            const int h = columnHeight(noise, x, z);
            if (!s.allowsBiome(columnBiome(noise, x, z, h))) return -1;
            if (h < kSeaLevel + 1) return -1;                 // no water
            if (h > 127 - s.sizeY() - 1) return -1;           // fits under the top
            if (riverMask(noise, x, z) > 0.05) return -1;     // not in a river
            lowest = std::min(lowest, h);
            highest = std::max(highest, h);
        }
    }

    if (highest < 0 || highest - lowest > kMaxGroundStep) return -1;
    return highest + 1;
}

// Pack the ground back up under the template.
//
// The structure rests on the HIGHEST column of its footprint, so on the
// one- or two-block dip that kMaxGroundStep allows there would be a gap
// under the floor and the temple would look like it was hovering. Filling
// with the biome's own surface block makes it sit in the sand.
void fillUnder(const TerrainNoise& noise, GenerationContext& ctx, const Structure& s,
               int wx, int baseY, int wz) {
    mcp_chunk_t* chunk = ctx.chunk;
    const int bx = ctx.chunk_x * 16;
    const int bz = ctx.chunk_z * 16;

    for (int lz = 0; lz < s.sizeZ(); ++lz) {
        for (int lx = 0; lx < s.sizeX(); ++lx) {
            const int x = wx + lx;
            const int z = wz + lz;
            if (x < bx || x >= bx + 16 || z < bz || z >= bz + 16) continue;

            const int h = columnHeight(noise, x, z);
            if (h >= baseY) continue;
            const uint8_t ground =
                biome::BiomeRegistry::forId(columnBiome(noise, x, z, h)).topBlock;
            for (int y = h; y < baseY; ++y)
                mcp_chunk_set_block_full(chunk, x - bx, y, z - bz, ground, 0);
        }
    }
}

} // namespace

void StructurePopulator::populate(GenerationContext& ctx) {
    if (structures_.empty()) return;

    // Regions are kSpacing chunks across, so only this region and its
    // eight neighbours can hold a structure that touches this chunk. The
    // anchors are worked out FIRST because that needs no noise at all,
    // and the overwhelming majority of chunks are not near one: building
    // the five simplex layers of a TerrainNoise for a chunk that cannot
    // host anything would be wasted work on every chunk of the world.
    const int regionX = (int)std::floor((double)ctx.chunk_x / kSpacing);
    const int regionZ = (int)std::floor((double)ctx.chunk_z / kSpacing);

    // A candidate is a structure together with the anchor it was drawn
    // at: the anchor has to travel with it, because a template wide
    // enough to reach this chunk from a NEIGHBOURING region must be
    // placed at that region's anchor, not at this chunk's own.
    struct Candidate {
        const Structure* structure;
        int wx, wz;
    };
    std::vector<Candidate> candidates;

    for (int rx = regionX - 1; rx <= regionX + 1; ++rx) {
        for (int rz = regionZ - 1; rz <= regionZ + 1; ++rz) {
            int anchorX, anchorZ;
            regionCandidate(ctx.seed, rx, rz, kSalt, anchorX, anchorZ);
            const int wx = anchorX * 16;
            const int wz = anchorZ * 16;
            for (const auto& s : structures_) {
                // The anchor chunk is the structure's local (0,0,0) corner,
                // so a template that fits in 16 x 16 (the desert temple is
                // 11 x 10) can never reach into the next chunk at all. A
                // bigger one would simply be written by every chunk it
                // touches, each re-deriving the same anchor from the seed —
                // the same way PopulatorTree shares a canopy across a
                // border, so it is never half a structure either.
                if (s->overlapsChunk(ctx.chunk_x, ctx.chunk_z, wx, wz))
                    candidates.push_back({s.get(), wx, wz});
            }
        }
    }
    if (candidates.empty()) return;

    TerrainNoise noise(ctx.seed);
    for (const Candidate& c : candidates) {
        const int baseY = structureBaseY(noise, *c.structure, c.wx, c.wz);
        if (baseY < 0) continue;

        fillUnder(noise, ctx, *c.structure, c.wx, baseY, c.wz);
        if (c.structure->placeInto(ctx.chunk, ctx.chunk_x, ctx.chunk_z, c.wx, baseY, c.wz))
            ctx.chunk->dirty = true;
    }
}

} // namespace cursestone::world::generator::populator