// tools/plains_check.cpp
//
// Measures the things that decide whether the world looks like the target
// screenshot: broad flat plains sitting against the ocean, sandy beaches
// along the sea but only rare long sand banks on rivers, and trees that
// neither grow in water nor saturate the plains.
//
//   g++ -std=c++20 -Isrc tools/plains_check.cpp <generator objects> -lz -o plains_check
#include "level/generator/Normal.h"
#include "level/generator/TerrainShape.h"
#include "level/generator/biome/Biome.h"
#include "level/format/Chunk.h"
#include "block/BlockIds.h"

#include <cstdio>
#include <cmath>
#include <map>
#include <vector>
#include <functional>
#include <algorithm>

using namespace cursestone::level::generator;
namespace biome = cursestone::level::generator::biome;
using cursestone::block::BlockId;
namespace blk = cursestone::block;

static const int R = 10;
static const int CW = (2 * R + 1) * 16;

int main() {
    TerrainNoise noise(12345);

    std::vector<int>     hgt(CW * CW);
    std::vector<uint8_t> bio(CW * CW);
    std::vector<uint8_t> top(CW * CW);
    std::vector<double>  riv(CW * CW);
    std::vector<uint8_t> hasTree(CW * CW, 0);
    std::vector<uint8_t> hasTrunk(CW * CW, 0);
    // The WIDE valley mask, not the narrow channel mask. A sand bank sits on
    // the bank of the river, which is by definition OUTSIDE the channel, so
    // riverMask() is ~0 there and testing it finds nothing at all.
    std::vector<double> val(CW * CW);

    // Array indexing only. The grids are sized CW*CW and every analysis
    // loop below walks them with plain array coordinates, so this must NOT
    // shift by R*16 -- the world->array shift happens once, in the fill.
    auto idx = [](int x, int z) { return z * CW + x; };

    for (int cz = -R; cz <= R; ++cz) {
        for (int cx = -R; cx <= R; ++cx) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            GenerationContext ctx{};
            ctx.seed = 12345;
            ctx.chunk_x = cx;
            ctx.chunk_z = cz;
            ctx.chunk = c;
            ctx.biomes_in = nullptr;
            Normal gen(12345);
            gen.generate(ctx);

            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    const int wx = cx * 16 + x, wz = cz * 16 + z;
                    const int i = idx(wx + R * 16, wz + R * 16);
                    const int h = mcp_chunk_get_height_map(c, x, z);
                    hgt[i] = h;
                    bio[i] = mcp_chunk_get_biome(c, x, z);
                    top[i] = mcp_chunk_get_block_id(c, x, h - 1, z);
                    riv[i] = riverMask(noise, wx, wz);
                    val[i] = riverValley(noise, wx, wz);
                }

            gen.populate(ctx);   // trees go in here

            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    const int wx = cx * 16 + x, wz = cz * 16 + z;
                    const int i = idx(wx + R * 16, wz + R * 16);
                    for (int y = hgt[i]; y < hgt[i] + 10 && y < 128; ++y) {
                        const uint8_t b = mcp_chunk_get_block_id(c, x, y, z);
                        if (b == (uint8_t)blk::BlockId::LOG ||
                            b == (uint8_t)blk::BlockId::WOOD_2) {
                            hasTree[i] = 1;
                            // A TRUNK is a log sitting exactly on the ground
                            // of its own column. A log higher up in a
                            // neighbouring column is canopy overhanging the
                            // water, which is normal and not the bug the
                            // user reported -- only the trunk can stand in
                            // a river, so only the trunk is worth counting.
                            if (y == hgt[i]) hasTrunk[i] = 1;
                            break;
                        }
                    }
                }
            mcp_chunk_destroy(c);
        }
    }

    const long cols = (long)CW * CW;

    // True when any column in the square ring at Chebyshev distance <=
    // `radius` satisfies `pred`. Bounds-checked on every side; the old
    // hand-rolled diagonal walk ran off the end of the grid.
    auto withinRing = [&](int x, int z, int radius, const std::function<bool(int, int)>& pred) {
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) == 0) continue;
                const int ax = x + dx, az = z + dz;
                if (ax < 0 || ax >= CW || az < 0 || az >= CW) continue;
                if (pred(ax, az)) return true;
            }
        return false;
    };

    // ---- 1. how much of the land is plains, and how wide are the stretches
    long land = 0, plains = 0;
    for (long i = 0; i < cols; ++i)
        if (hgt[i] > kSeaLevel) { ++land; if (bio[i] == biome::BIOME_PLAINS) ++plains; }
    printf("=== PLAINS ===\n");
    printf("plains: %ld of %ld land columns (%.1f%% of land)\n",
           plains, land, land ? 100.0 * plains / land : 0.0);

    std::vector<int> runs;
    for (int row = 0; row < CW; row += 2) {
        int run = 0;
        for (int col = 0; col <= CW; ++col) {
            const bool isPlains = col < CW && hgt[idx(col, row)] > kSeaLevel &&
                                  bio[idx(col, row)] == biome::BIOME_PLAINS;
            if (isPlains) ++run;
            else { if (run > 0) runs.push_back(run); run = 0; }
        }
    }
    std::sort(runs.begin(), runs.end());
    if (!runs.empty())
        printf("plains strip width across transects: median=%d p90=%d max=%d blocks\n",
               runs[runs.size() / 2], runs[(size_t)(runs.size() * 0.9)], runs.back());

    // ---- 2. are the plains next to the ocean?
    long nearOcean = 0;
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            const int i = idx(x, z);
            if (hgt[i] <= kSeaLevel || bio[i] != biome::BIOME_PLAINS) continue;
            if (withinRing(x, z, 32, [&](int ax, int az) {
                    return bio[idx(ax, az)] == biome::BIOME_OCEAN; }))
                ++nearOcean;
        }
    printf("plains within 32 blocks of ocean: %.1f%%\n",
           plains ? 100.0 * nearOcean / plains : 0.0);

    // ---- 3. how flat is the plains, compared to the REST OF THE LAND
    // Compared against land only. Including ocean and beach in the "else"
    // bucket drags the average down with flat seabed and would hide whether
    // the plains are actually flatter than the terrain around them.
    long pp = 0, ps = 0, ep = 0, es = 0;
    for (int z = 0; z < CW - 1; ++z)
        for (int x = 0; x < CW - 1; ++x) {
            const int i = idx(x, z), j = idx(x + 1, z);
            if (hgt[i] <= kSeaLevel) continue;
            const int d = std::abs(hgt[i] - hgt[j]);
            if (bio[i] == biome::BIOME_PLAINS) { ++pp; ps += d; }
            else { ++ep; es += d; }
        }
    printf("avg height step: plains %.2f vs other land %.2f\n",
           pp ? (double)ps / pp : 0.0, ep ? (double)es / ep : 0.0);

    // ---- 4. where does sand actually appear?
    printf("\n=== SAND ===\n");
    long sandOcean = 0, sandRiver = 0, sandOther = 0;
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            const int i = idx(x, z);
            if (top[i] != (uint8_t)blk::BlockId::SAND) continue;
            if (hgt[i] < kSeaLevel) { ++sandOcean; continue; }   // sea/river bed
            // Above water: is the nearby water sea or a river?
            bool sea = false, riv2 = false;
            withinRing(x, z, 4, [&](int ax, int az) {
                if (hgt[idx(ax, az)] >= kSeaLevel) return false;
                if (val[idx(ax, az)] > 0.06) riv2 = true; else sea = true;
                return false;   // keep scanning: we want both flags
            });
            if (sea) ++sandOcean; else if (riv2) ++sandRiver; else ++sandOther;
        }
    printf("sand columns: sea/beach %ld, river bank %ld, neither %ld\n",
           sandOcean, sandRiver, sandOther);
    printf("river sand banks are %.2f%% of all sand\n",
           (sandOcean + sandRiver + sandOther)
               ? 100.0 * sandRiver / (sandOcean + sandRiver + sandOther) : 0.0);

    // ---- 5. are the river sand banks LONG when they happen?
    std::vector<int> bankRuns;
    for (int row = 0; row < CW; row += 2) {
        int run = 0;
        for (int col = 0; col <= CW; ++col) {
            bool isBank = false;
            if (col < CW) {
                const int i = idx(col, row);
                if (top[i] == (uint8_t)blk::BlockId::SAND && hgt[i] >= kSeaLevel &&
                    val[i] > 0.06) {
                    // A bank only counts as a RIVER bank when it cannot see
                    // open sea within 3 blocks -- a beach is never a bank.
                    const bool seaNear = withinRing(col, row, 3, [&](int ax, int az) {
                        return hgt[idx(ax, az)] < kSeaLevel && val[idx(ax, az)] < 0.06;
                    });
                    isBank = !seaNear;
                }
            }
            if (isBank) ++run;
            else { if (run > 0) bankRuns.push_back(run); run = 0; }
        }
    }
    std::sort(bankRuns.begin(), bankRuns.end());
    if (!bankRuns.empty()) {
        int sum = 0; for (int v : bankRuns) sum += v;
        printf("river sand banks: %ld stretches seen, mean %.0f blocks, "
               "longest %d\n", bankRuns.size(), (double)sum / bankRuns.size(),
               bankRuns.back());
    } else {
        printf("river sand banks: none\n");
    }

    // ---- 6. trees must never grow in water
    printf("\n=== TREES ===\n");
    long treesInWater = 0, trunksInWater = 0, treesOnShore = 0, treesTotal = 0,
         plainsTrees = 0;
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            const int i = idx(x, z);
            if (!hasTree[i]) continue;
            ++treesTotal;
            if (hgt[i] < kSeaLevel) ++treesInWater;
            if (riv[i] > 0.05) ++treesInWater;
            // The bug the user reported: a trunk rooted under the water.
            if (hasTrunk[i] && (hgt[i] < kSeaLevel || riv[i] > 0.05)) ++trunksInWater;
            if (isShore(noise, x - R * 16, z - R * 16, hgt[i])) ++treesOnShore;
            if (bio[i] == biome::BIOME_PLAINS) ++plainsTrees;
        }
    printf("tree columns: %ld (%.2f%% of all columns)\n", treesTotal,
           100.0 * treesTotal / cols);
    long trunks = 0;
    for (long i = 0; i < cols; ++i) if (hasTrunk[i]) ++trunks;
    printf("trunks: %ld (%.2f%% of all columns)\n", trunks, 100.0 * trunks / cols);
    printf("  trunks rooted in water   : %ld  <-- must be 0\n", trunksInWater);
    printf("  canopy over water (ok)   : %ld\n", treesInWater);
    printf("  on the water's edge      : %ld\n", treesOnShore);
    printf("  standing in plains       : %ld (%.1f%% of all trees)\n", plainsTrees,
           treesTotal ? 100.0 * plainsTrees / treesTotal : 0.0);

    // ---- 7. spacing between neighbouring tree TRUNKS in the plains
    // Trunks, not canopy columns: two oaks 4 blocks apart have canopies
    // that overlap, so counting log blocks in every column reports them as
    // 1 block apart when the trunks are actually where they were asked to be.
    std::vector<int> nn;
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            const int i = idx(x, z);
            if (!hasTrunk[i] || bio[i] != biome::BIOME_PLAINS) continue;
            int best = 9999;
            for (int dz = -12; dz <= 12; ++dz)
                for (int dx = -12; dx <= 12; ++dx) {
                    if (!dx && !dz) continue;
                    const int ax = x + dx, az = z + dz;
                    if (ax < 0 || ax >= CW || az < 0 || az >= CW) continue;
                    if (!hasTrunk[idx(ax, az)]) continue;
                    const int d2 = dx * dx + dz * dz;
                    if (d2 < best) best = d2;
                }
            if (best < 9999) nn.push_back((int)std::lround(std::sqrt((double)best)));
        }
    std::sort(nn.begin(), nn.end());
    if (!nn.empty()) {
        long tooClose = 0;
        for (int v : nn) if (v < 3) ++tooClose;
        printf("plains trees: nearest neighbour median=%d blocks, "
               "pairs closer than 3: %ld (%.1f%%)\n",
               nn[nn.size() / 2], tooClose, 100.0 * (double)tooClose / (double)nn.size());
    }

    printf("\nPLAINS CHECK: done\n");
    return 0;
}