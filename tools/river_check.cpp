// tools/river_check.cpp
//
// Measures the river / shoreline behaviour of the world generator so the
// tuning can be checked with numbers instead of by eye:
//
//   * how WIDE the water of a river is (vanilla rivers are narrow)
//   * how DEEP the channel is cut below sea level (a canyon shows up here)
//   * how STEEP the banks are (max height step near a river)
//   * whether SAND appears on the banks at/above the water line
//   * how much GRAVEL and exposed STONE there is
//   * whether rare tree-less patches exist near rivers and plains
//
//   g++ -std=c++20 -Isrc tools/river_check.cpp <generator objects> -lz -o river_check
#include "world/generator/Normal.h"
#include "world/generator/TerrainShape.h"
#include "world/generator/biome/Biome.h"
#include "world/format/Chunk.h"
#include "block/BlockIds.h"

#include <cstdio>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace cursestone::world::generator;
using cursestone::block::BlockId;
namespace blk = cursestone::block;
namespace biome = cursestone::world::generator::biome;

int main() {
    TerrainNoise noise(12345);

    const int R = 10;                 // chunks -R..R
    const int CW = (2 * R + 1) * 16;  // world columns per axis

    std::vector<int>   hgt(CW * CW);
    std::vector<uint8_t> bio(CW * CW);
    std::vector<uint8_t> top(CW * CW);
    std::vector<double> riv(CW * CW);
    std::vector<uint8_t> tree(CW * CW);

    // ---- Pass 1: heights, biomes and surface blocks ----------------------
    long sandTop = 0, gravelTop = 0, stoneTop = 0, water = 0;
    long sandDry = 0, gravelDry = 0, oceanCols = 0;
    long beachColumns = 0, grassColumns = 0;
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

            // Terrain stats are read BEFORE populate(): populate() raises the
            // height map to the top of the trees/vegetation, which would make
            // the surface-block and tree lookups point at the canopy instead
            // of at the ground.
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    const int wx = cx * 16 + x;
                    const int wz = cz * 16 + z;
                    const int i = (wz + R * 16) * CW + (wx + R * 16);
                    const int h = mcp_chunk_get_height_map(c, x, z);
                    const uint8_t b = mcp_chunk_get_biome(c, x, z);
                    const uint8_t t = mcp_chunk_get_block_id(c, x, h - 1, z);
                    hgt[i] = h;
                    bio[i] = b;
                    top[i] = t;
                    riv[i] = riverMask(noise, wx, wz);
                    if (h < kSeaLevel) ++water;
                    // Sand is what matters ABOVE the water line: that is the
                    // bank the player walks on. Sand on the sea floor is
                    // normal vanilla and would drown the signal here.
                    if (t == (uint8_t)blk::BlockId::SAND) {
                        ++sandTop;
                        if (h >= kSeaLevel) ++sandDry;
                    }
                    if (t == (uint8_t)blk::BlockId::GRAVEL) {
                        ++gravelTop;
                        if (h >= kSeaLevel) ++gravelDry;
                    }
                    if (t == (uint8_t)blk::BlockId::STONE) ++stoneTop;
                    if (b == biome::BIOME_BEACH) ++beachColumns;
                    if (t == (uint8_t)blk::BlockId::GRASS) ++grassColumns;
                    if (b == biome::BIOME_OCEAN) ++oceanCols;
                }
            }

            // Trees (and ores/caves) are placed here.
            gen.populate(ctx);

            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    const int wx = cx * 16 + x;
                    const int wz = cz * 16 + z;
                    const int i = (wz + R * 16) * CW + (wx + R * 16);
                    bool hasLog = false;
                    for (int y = hgt[i]; y < hgt[i] + 8 && y < 128; ++y) {
                        const uint8_t bl = mcp_chunk_get_block_id(c, x, y, z);
                        if (bl == (uint8_t)blk::BlockId::LOG ||
                            bl == (uint8_t)blk::BlockId::WOOD_2) { hasLog = true; break; }
                    }
                    tree[i] = hasLog ? 1 : 0;
                }
            }
            mcp_chunk_destroy(c);
        }
    }

    const long cols = (long)CW * CW;
    printf("columns=%ld water=%ld (%.1f%%)  ocean=%ld (%.1f%%)  beach=%ld (%.1f%%)\n",
           cols, water, 100.0 * water / cols, oceanCols,
           100.0 * oceanCols / cols, beachColumns,
           100.0 * beachColumns / cols);
    printf("surface: sand=%.2f%% (dry %.2f%%)  gravel=%.2f%% (dry %.2f%%)  "
           "stone=%.2f%%  grass=%.2f%%\n",
           100.0 * sandTop / cols, 100.0 * sandDry / cols,
           100.0 * gravelTop / cols, 100.0 * gravelDry / cols,
           100.0 * stoneTop / cols, 100.0 * grassColumns / cols);

    // ---- River water width ----------------------------------------------
    // Walk a transect across the map and measure each contiguous run of
    // river columns (biome RIVER, or below sea level with a river mask).
    std::vector<int> widths, depths, bankSteps;
    int transects = 0;
    for (int row = 0; row < CW; row += 3) {
        ++transects;
        int run = 0;
        std::vector<int> runCols;
        auto flush = [&]() {
            if (run >= 2) {
                widths.push_back(run);
                // deepest point of the channel relative to sea level
                int deepest = 0;
                for (int c : runCols) {
                    const int i = row * CW + c;
                    deepest = std::max(deepest, kSeaLevel - hgt[i]);
                }
                depths.push_back(deepest);
            }
            run = 0;
            runCols.clear();
        };
        for (int col = 0; col < CW; ++col) {
            const int i = row * CW + col;
            const bool isRiver =
                bio[i] == biome::BIOME_RIVER || bio[i] == biome::BIOME_FROZEN_RIVER;
            if (isRiver && hgt[i] < kSeaLevel) {
                ++run;
                runCols.push_back(col);
            } else {
                flush();
            }
        }
        flush();
    }
    auto stat = [&](std::vector<int>& v, const char* name) {
        if (v.empty()) { printf("%s: none\n", name); return; }
        std::sort(v.begin(), v.end());
        double sum = 0; for (int x : v) sum += x;
        printf("%s: n=%zu min=%d median=%d mean=%.1f p90=%d max=%d\n",
               name, v.size(), v.front(), v[v.size() / 2], sum / v.size(),
               v[(size_t)(v.size() * 0.9)], v.back());
    };
    printf("\n--- rivers (across %d transects) ---\n", transects);
    stat(widths, "water width (blocks)");
    stat(depths, "channel depth below sea level");

    // ---- Bank steepness ---------------------------------------------------
    // The height step between neighbours within 6 blocks of a river is what
    // turns a valley into a canyon, so measure it there specifically.
    long nearSteps = 0, nearPairs = 0, farSteps = 0, farPairs = 0;
    int nearMax = 0;
    std::vector<int> nearStepHist;
    for (int z = 1; z < CW - 1; ++z) {
        for (int x = 1; x < CW - 1; ++x) {
            const int i = z * CW + x;
            const bool nearRiver = riv[i] > 0.02 || bio[i] == biome::BIOME_RIVER;
            for (int d = 1; d <= 6; ++d) {
                const int j = i + d;
                const int step = std::abs(hgt[i] - hgt[j]);
                if (nearRiver) {
                    ++nearPairs; nearSteps += step;
                    nearMax = std::max(nearMax, step);
                    nearStepHist.push_back(step);
                } else {
                    ++farPairs;  farSteps += step;
                }
            }
        }
    }
    std::sort(nearStepHist.begin(), nearStepHist.end());
    auto pct = [&](double p) {
        if (nearStepHist.empty()) return 0;
        size_t k = (size_t)(nearStepHist.size() * p);
        if (k >= nearStepHist.size()) k = nearStepHist.size() - 1;
        return nearStepHist[k];
    };
    printf("\n--- relief ---\n");
    printf("avg step near rivers: %.2f  p50=%d p90=%d p99=%d p99.9=%d max=%d\n",
           nearPairs ? (double)nearSteps / nearPairs : 0.0,
           pct(0.50), pct(0.90), pct(0.99), pct(0.999), nearMax);
    printf("avg step elsewhere  : %.2f\n",
           farPairs ? (double)farSteps / farPairs : 0.0);
    long cliffs = 0;
    for (int s : nearStepHist) if (s >= 5) ++cliffs;
    printf("near-river samples with a 5+ block step: %ld (%.3f%%)\n",
           cliffs, nearStepHist.empty() ? 0.0 : 100.0 * cliffs / nearStepHist.size());

    // ---- Bare tree patches -------------------------------------------------
    // A bare patch is a >=5x5 block of land columns with no log block on top.
    long bare = 0, landWindows = 0, bareNearRiver = 0, windowsNearRiver = 0;
    const int K = 24;
    for (int z = 0; z + K <= CW; z += K) {
        for (int x = 0; x + K <= CW; x += K) {
            long logs = 0, land = 0, nearR = 0;
            for (int b = 0; b < K; ++b) {
                for (int a = 0; a < K; ++a) {
                    const int i = (z + b) * CW + (x + a);
                    if (hgt[i] > kSeaLevel) ++land;
                    if (riv[i] > 0.02) ++nearR;
                    logs += tree[i];
                }
            }
            if (land < K * K / 4) continue;
            (void)0;
            ++landWindows;
            if (logs == 0) {
                ++bare;
                if (nearR > K * K / 30) ++bareNearRiver;
            }
            if (nearR > K * K / 30) ++windowsNearRiver;
        }
    }
    long treeCols = 0;
    for (long i = 0; i < cols; ++i) treeCols += tree[i];
    printf("\n--- vegetation ---\n");
    printf("columns carrying a tree: %ld (%.2f%%)\n", treeCols,
           100.0 * treeCols / cols);

    // Direct measurement of the rare tree-less clearings: compare tree
    // coverage inside the clearing mask against the rest of the land. A
    // fixed 24x24 window mostly measures "is this a forest biome", so this
    // asks the question the feature actually answers.
    long inLand = 0, inTree = 0, outLand = 0, outTree = 0;
    long maskLand = 0, maskNearRiver = 0;
    for (int z = 0; z < CW; ++z) {
        for (int x = 0; x < CW; ++x) {
            const int i = z * CW + x;
            if (hgt[i] <= kSeaLevel) continue;      // only land
            const double m = treeFreeMask(noise, x - R * 16, z - R * 16, hgt[i]);
            ++inLand;
            if (m > 0.30) {
                ++maskLand;
                inTree += tree[i];
                if (riv[i] > 0.02) ++maskNearRiver;
            } else {
                ++outLand;
                outTree += tree[i];
            }
        }
    }
    printf("tree-free clearing mask covers %ld land columns (%.2f%% of land), "
           "%ld of them beside a river\n",
           maskLand, inLand ? 100.0 * maskLand / inLand : 0.0, maskNearRiver);
    printf("tree coverage inside the mask : %.2f%%\n",
           maskLand ? 100.0 * (double)inTree / maskLand : 0.0);
    printf("tree coverage outside the mask: %.2f%%\n",
           outLand ? 100.0 * (double)outTree / outLand : 0.0);
    printf("bare windows: %ld/%ld (%.1f%%), of those near rivers: %ld/%ld (%.1f%%)\n",
           bare, landWindows, landWindows ? 100.0 * bare / landWindows : 0.0,
           bareNearRiver, windowsNearRiver,
           windowsNearRiver ? 100.0 * bareNearRiver / windowsNearRiver : 0.0);

    printf("\nRIVER CHECK: done\n");
    return 0;
}