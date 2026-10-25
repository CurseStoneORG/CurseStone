// tools/biome_width_check.cpp
//
// Measures how THIN the generated land biomes are, so the "biomas muy
// delgados" complaint can be checked with numbers instead of by eye.
//
// For every land column the thickness metric is the minimum, over the four
// cardinal directions, of the run length of its own biome until a different
// biome appears (capped). A biome that only ever appears in 1-6 block
// ribbons reports a tiny min/p5 thickness and a large "thin share".
//
//   g++ -std=c++20 -O2 -w -Isrc -o biome_width_check tools/biome_width_check.cpp \
//       src/world/generator/TerrainShape.cpp src/world/generator/noise/*.cpp \
//       src/world/generator/biome/*.cpp src/world/format/Chunk.c ... (see run script)
//
// Usage: biome_width_check [seed]
#include "world/generator/TerrainShape.h"
#include "world/generator/biome/Biome.h"
#include "world/generator/biome/BiomeRegistry.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstddef>

using namespace cursestone::world::generator;
using namespace cursestone::world::generator::biome;

int main(int argc, char** argv) {
    const int64_t seed = argc > 1 ? atoll(argv[1]) : 12345;
    const int R = 24;                // chunks -R..R
    const int CW = (2 * R + 1) * 16; // world columns per axis
    const int CAP = 40;              // run-length cap

    TerrainNoise noise(seed);

    std::vector<uint8_t> bio((size_t)CW * CW);
    for (int z = 0; z < CW; ++z) {
        for (int x = 0; x < CW; ++x) {
            const int wx = x - R * 16;
            const int wz = z - R * 16;
            // Same call the generator uses; heights come from the pure
            // function so this matches what Normal::generate stores.
            const int h = columnHeight(noise, wx, wz);
            bio[(size_t)z * CW + x] = columnBiome(noise, wx, wz, h);
        }
    }

    // Per-column thickness: the MAXIMUM, over the four cardinal directions,
    // of the run length of its own biome until a different biome appears
    // (capped). A column inside a genuine sliver is thin in every direction,
    // so its max stays tiny; an edge column of a large biome is thin only
    // outward but deep (>= CAP) inward, so the max separates the two cases.
    std::vector<uint8_t> thick((size_t)CW * CW);
    auto biomeAt = [&](int x, int z) -> uint8_t {
        if (x < 0 || x >= CW || z < 0 || z >= CW)
            return 255; // outside: treat as boundary
        return bio[(size_t)z * CW + x];
    };
    for (int z = 0; z < CW; ++z) {
        for (int x = 0; x < CW; ++x) {
            const uint8_t b = bio[(size_t)z * CW + x];
            int best = 1;
            static const int dx[4] = {1, -1, 0, 0};
            static const int dz[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                int run = 0;
                int px = x, pz = z;
                while (run < CAP) {
                    px += dx[d];
                    pz += dz[d];
                    if (biomeAt(px, pz) != b)
                        break;
                    ++run;
                }
                best = std::max(best, run + 1);
            }
            thick[(size_t)z * CW + x] = (uint8_t)best;
        }
    }

    // Aggregate per biome.
    struct Stats {
        long cols = 0;
        std::vector<int> t;
    };
    std::map<uint8_t, Stats> stats;
    for (int z = 0; z < CW; ++z) {
        for (int x = 0; x < CW; ++x) {
            const uint8_t b = bio[(size_t)z * CW + x];
            if (b == BIOME_OCEAN || b == BIOME_RIVER || b == BIOME_FROZEN_RIVER ||
                b == BIOME_BEACH)
                continue; // water bodies and beaches are supposed to be narrow
            auto& s = stats[b];
            ++s.cols;
            s.t.push_back(thick[(size_t)z * CW + x]);
        }
    }

    printf("seed=%lld region=%dx%d columns (land biomes only)\n", (long long)seed, CW, CW);
    printf("%-18s %9s %7s %7s %7s %7s %8s\n", "biome", "columns", "min", "p1", "p5", "p50", "<8blk");
    long totalThin = 0, totalCols = 0;
    for (auto& kv : stats) {
        auto& s = kv.second;
        std::sort(s.t.begin(), s.t.end());
        auto at = [&](double p) {
            size_t k = (size_t)(s.t.size() * p);
            if (k >= s.t.size())
                k = s.t.size() - 1;
            return s.t[k];
        };
        long thin = 0;
        for (int t : s.t)
            if (t < 8)
                ++thin;
        totalThin += thin;
        totalCols += s.cols;
        printf("%-18s %9ld %7d %7d %7d %7d %7.2f%%\n", BiomeRegistry::forId(kv.first).name,
               s.cols, s.t.front(), at(0.01), at(0.05), at(0.50),
               100.0 * thin / s.t.size());
    }
    printf("\nland columns=%ld  thinner than 8 blocks: %ld (%.2f%%)\n", totalCols, totalThin,
           totalCols ? 100.0 * totalThin / totalCols : 0.0);

    // ---- Sliver map (argv[2] = dump size, e.g. 160) ----------------------
    // Finds the densest window of thin columns and prints it as ASCII so
    // the shape of the surviving slivers can actually be looked at.
    if (argc > 2) {
        const int W = atoi(argv[2]);
        if (W > 0 && W <= CW) {
            long bestCnt = -1, bx = 0, bz = 0;
            for (int z = 0; z + W <= CW; z += 4) {
                for (int x = 0; x + W <= CW; x += 4) {
                    long cnt = 0;
                    for (int b = 0; b < W; b += 2)
                        for (int a = 0; a < W; a += 2)
                            if (thick[(size_t)(z + b) * CW + x + a] < 8)
                                ++cnt;
                    if (cnt > bestCnt) {
                        bestCnt = cnt;
                        bx = x;
                        bz = z;
                    }
                }
            }
            printf("\nsliver map: %dx%d window at (%d,%d), thin samples=%ld\n"
                   "(legend: %c plains %c desert %c forest %c hills %c taiga %c ice %c jungle %c "
                   "savanna %c plateau %c swamp; letter = thin column of that biome, . = other, "
                   "~ = water/beach)\n",
                   W, W, bx - R * 16, bz - R * 16, bestCnt, 'P', 'D', 'F', 'H', 'T', 'I', 'J',
                   'S', 'V', 'W');
            for (int z = bz; z < bz + W; z += 2) {
                for (int x = bx; x < bx + W; x += 2) {
                    const uint8_t b = bio[(size_t)z * CW + x];
                    const bool thin = thick[(size_t)z * CW + x] < 8;
                    char c = '.';
                    switch (b) {
                    case BIOME_OCEAN:
                    case BIOME_RIVER:
                    case BIOME_FROZEN_RIVER:
                    case BIOME_BEACH: c = '~'; break;
                    case BIOME_PLAINS: c = 'P'; break;
                    case BIOME_DESERT: c = 'D'; break;
                    case BIOME_FOREST: c = 'F'; break;
                    case BIOME_EXTREME_HILLS: c = 'H'; break;
                    case BIOME_TAIGA: c = 'T'; break;
                    case BIOME_ICE_PLAINS: c = 'I'; break;
                    case BIOME_JUNGLE: c = 'J'; break;
                    case BIOME_SAVANNA: c = 'S'; break;
                    case BIOME_SAVANNA_PLATEAU: c = 'V'; break;
                    case BIOME_SWAMPLAND: c = 'W'; break;
                    case BIOME_PLAINS_PLATEAU: c = 'A'; break;
                    default: c = '?'; break;
                    }
                    if (!thin && c != '~')
                        c = (char)std::tolower(c);
                    std::putchar(c);
                }
                std::putchar('\n');
            }
        }
    }


    // Distribution of the 0.5 percentile of thickness across all biomes with
    // at least 100 columns: a healthy generator keeps p5 >= ~10 everywhere.
    return 0;
}
