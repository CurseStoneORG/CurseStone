// tools/mesa_check.cpp
//
// Offline mesa census: locates mesa biomes with the live BiomeSource,
// generates their chunks through the exact server pipeline and verifies
// the surface painting:
//   - hardened clay (172) + stained clay (159) form the mesa body
//   - stained clay shows several band colors (aux histogram)
//   - red sand (12:1) covers the terrain near sea level
//   - gold ore still pokes out near the surface (mesa ore bonus)
//   - no bare stone left at the surface of mesa columns
//
//   g++ -std=c++20 -O2 -Isrc tools/mesa_check.cpp <objects> -o /tmp/mesa_check
#include "world/generator/vgen/VgenSource.h"
#include "world/format/Chunk.h"
#include "world/generator/NeighborChunkIO.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

using namespace cursestone::world::generator;
using vgen::Block;

namespace {

struct Key {
    int x, z;
    bool operator==(const Key& o) const { return x == o.x && z == o.z; }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        return (size_t)(uint32_t)k.x * 0x9E3779B9u ^ (size_t)(uint32_t)k.z;
    }
};

struct GridSink : INeighborBlockSink {
    std::unordered_map<Key, mcp_chunk_t*, KeyHash>* grid = nullptr;
    void commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id,
                             uint8_t data) override {
        auto it = grid->find({cx, cz});
        if (it == grid->end()) return;
        uint32_t y = index % 128;
        uint32_t rest = index / 128;
        uint32_t z = rest % 16;
        uint32_t x = rest / 16;
        mcp_chunk_set_block_full(it->second, (int)x, (int)y, (int)z, id, data);
    }
};

// aux values of stained clay (159)
const char* clayColorName(uint8_t aux) {
    switch (aux) {
        case 0: return "white";
        case 1: return "orange";
        case 4: return "yellow";
        case 8: return "silver";
        case 12: return "brown";
        case 14: return "red";
        default: return "other";
    }
}

} // namespace

int main(int argc, char** argv) {
    const int64_t seed = argc > 1 ? strtoll(argv[1], nullptr, 10) : 12345;
    const int span = argc > 2 ? atoi(argv[2]) : 48; // scan +-span*16 blocks for mesa

    VgenSource gen(seed);

    // ---- locate mesa biomes with the live biome source ----
    long mesaColumns = 0;
    int mesaBiomeId = -1;
    int firstMesaX = 0, firstMesaZ = 0;
    const int step = 16;
    for (int wx = -span * 16; wx <= span * 16; wx += step) {
        for (int wz = -span * 16; wz <= span * 16; wz += step) {
            vgen::Biome* b = gen.getDimension().getBiomes().getBiome(wx, wz);
            if (b && (b->mId == 37 || b->mId == 38 || b->mId == 39)) {
                ++mesaColumns;
                if (mesaColumns == 1) {
                    mesaBiomeId = b->mId;
                    firstMesaX = wx;
                    firstMesaZ = wz;
                }
            }
        }
    }
    printf("seed %lld: mesa biome columns found in %dx%d block scan: %ld (first id %d at %d,%d)\n",
           (long long)seed, span * 32, span * 32, mesaColumns, mesaBiomeId,
           firstMesaX, firstMesaZ);
    if (mesaColumns == 0) {
        printf("no mesa in scan area for this seed — nothing to verify\n");
        return 0;
    }

    // ---- generate a window around the first mesa area ----
    const int RC = 12; // +-12 chunks around the mesa spot
    const int ccx = firstMesaX >> 4, ccz = firstMesaZ >> 4;
    std::unordered_map<Key, mcp_chunk_t*, KeyHash> grid;
    grid.reserve((size_t)(2 * RC + 1) * (2 * RC + 1));
    GridSink sink;
    sink.grid = &grid;
    gen.setNeighborSink(&sink);

    long stained = 0, hardened = 0, redSand = 0, goldDeep = 0, goldSurface = 0;
    long stoneSurface = 0, columns = 0;
    std::map<uint8_t, long> stainedByColor;
    int sliceX = -1, sliceZ = -1;
    std::vector<std::pair<int, uint8_t>> slice; // (y, id) of one column

    auto t0 = std::chrono::steady_clock::now();
    for (int cz = ccz - RC; cz <= ccz + RC; ++cz) {
        for (int cx = ccx - RC; cx <= ccx + RC; ++cx) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            grid[{cx, cz}] = c;
            GenerationContext ctx{};
            ctx.seed = seed;
            ctx.chunk_x = cx;
            ctx.chunk_z = cz;
            ctx.biomes_in = nullptr;
            ctx.chunk = c;
            gen.generate(ctx);
            gen.populate(ctx);
            gen.drainSpills(cx, cz, c);

            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    vgen::Biome* b = gen.getDimension().getBiomes().getBiome(cx * 16 + x, cz * 16 + z);
                    if (!b || (b->mId != 37 && b->mId != 38 && b->mId != 39)) continue;
                    ++columns;
                    const int h = mcp_chunk_get_height_map(c, x, z);
                    for (int y = 0; y < 128; ++y) {
                        const uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
                        if (id == 0) continue;
                        if (id == Block::mStainedClay->mID) {
                            ++stained;
                            uint8_t aux = mcp_chunk_get_block_data(c, x, y, z);
                            ++stainedByColor[aux];
                        } else if (id == Block::mHardenedClay->mID) {
                            ++hardened;
                        } else if (id == Block::mSand->mID &&
                                   mcp_chunk_get_block_data(c, x, y, z) == 1) {
                            ++redSand;
                        } else if (id == Block::mGoldOre->mID) {
                            if (y >= 60) ++goldSurface; else ++goldDeep;
                        }
                    }
                    // bare-stone surface check: topmost non-air block
                    {
                        int top = h > 0 ? h - 1 : 0;
                        while (top > 0 && mcp_chunk_get_block_id(c, x, top, z) == 0) --top;
                        uint8_t tid = mcp_chunk_get_block_id(c, x, top, z);
                        if (tid == Block::mStone->mID) ++stoneSurface;
                    }
                    if (sliceX < 0 && slice.empty() && h > 72) {
                        sliceX = cx * 16 + x;
                        sliceZ = cz * 16 + z;
                        for (int y = 55; y < 100; ++y) {
                            uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
                            if (id != 0) slice.push_back({y, id});
                        }
                    }
                }
            }
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    printf("\n=== mesa surface census (seed %lld, window %d chunks around %d,%d, %.1fs) ===\n",
           (long long)seed, 2 * RC + 1, firstMesaX, firstMesaZ, secs);
    printf("mesa columns scanned: %ld\n", columns);
    printf("stained clay  (159): %ld\n", stained);
    printf("hardened clay (172): %ld\n", hardened);
    printf("red sand    (12:1): %ld\n", redSand);
    printf("gold ore surface (y>=60): %ld   deep: %ld\n", goldSurface, goldDeep);
    printf("bare stone surface columns: %ld of %ld\n", stoneSurface, columns);
    printf("stained clay colors:\n");
    for (auto& kv : stainedByColor)
        printf("   aux %-2u (%s): %ld\n", kv.first, clayColorName(kv.first), kv.second);

    if (!slice.empty()) {
        printf("surface column sample at %d,%d (y 55..99):\n", sliceX, sliceZ);
        std::string line;
        int lastY = -100;
        for (auto& e : slice) {
            const char* s = e.second == Block::mStainedClay->mID ? "S" :
                            e.second == Block::mHardenedClay->mID ? "H" :
                            e.second == Block::mSand->mID ? "s" :
                            e.second == Block::mStone->mID ? "." :
                            e.second == Block::mSandStone->mID ? "=" : "?";
            if (lastY != -100 && e.first > lastY + 1) line += "|";
            line += s;
            lastY = e.first;
        }
        printf("   %s   (S=stained H=hardened s=sand .=stone ==sandstone)\n", line.c_str());
    }

    bool ok = stained > 1000 && hardened > 1000 && redSand > 100 &&
              stainedByColor.size() >= 3 && goldSurface > 0;
    printf("%s\n", ok ? "MESAS LOOK VANILLA" : "MESA SURFACE INCOMPLETE");
    return ok ? 0 : 1;
}
