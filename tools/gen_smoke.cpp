// Smoke test for the world generator. Generates a grid of chunks and
// prints block + biome statistics so we can confirm terrain, ores, caves
// and vegetation are actually produced (and that generation does not crash).
//
//   g++ -std=c++20 -Isrc tools/gen_smoke.cpp <generator sources> ... -o /tmp/gen_smoke
#include "level/generator/Normal.h"
#include "level/format/Chunk.h"
#include "block/BlockIds.h"

#include <cstdio>
#include <map>
#include <string>

using namespace cursestone::level::generator;
using cursestone::block::BlockId;

int main() {
    Normal gen(12345);

    long totals[256] = {0};
    std::map<uint8_t, long> biomes;
    int trees = 0, ores = 0, caves = 0, grass = 0, water = 0, sand = 0;
    int minH = 999, maxH = -1;

    const int R = 6; // -R..R chunks
    for (int cxi = -R; cxi <= R; ++cxi) {
        for (int czi = -R; czi <= R; ++czi) {
            mcp_chunk_t* c = mcp_chunk_new(cxi, czi);
            GenerationContext ctx{};
            ctx.seed = 12345;
            ctx.chunk_x = cxi;
            ctx.chunk_z = czi;
            ctx.biomes_in = nullptr;
            ctx.chunk = c;

            gen.generate(ctx);
            gen.populate(ctx);

            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    int h = mcp_chunk_get_height_map(c, x, z);
                    if (h < minH) minH = h;
                    if (h > maxH) maxH = h;
                    biomes[mcp_chunk_get_biome(c, x, z)]++;
                    for (int y = 0; y < 128; ++y) {
                        uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
                        totals[id]++;
                        if (id == (uint8_t)BlockId::LOG || id == (uint8_t)BlockId::WOOD_2) trees++;
                        if (id == (uint8_t)BlockId::COAL_ORE || id == (uint8_t)BlockId::IRON_ORE ||
                            id == (uint8_t)BlockId::GOLD_ORE || id == (uint8_t)BlockId::DIAMOND_ORE ||
                            id == (uint8_t)BlockId::REDSTONE_ORE) ores++;
                        if (id == (uint8_t)BlockId::GRASS) grass++;
                        if (id == (uint8_t)BlockId::WATER) water++;
                        if (id == (uint8_t)BlockId::SAND) sand++;
                        if (id == (uint8_t)BlockId::AIR && y > 5 && y < 60 &&
                            mcp_chunk_get_block_id(c, x, y + 3, z) != (uint8_t)BlockId::AIR) caves++;
                    }
                }
            }
            mcp_chunk_destroy(c);
        }
    }

    std::printf("height range: %d..%d\n", minH, maxH);
    std::printf("trees(log blocks)=%d ores=%d cave-air=%d grass=%d water=%d sand=%d\n",
                trees, ores, caves, grass, water, sand);
    std::printf("stone=%ld dirt=%ld bedrock=%ld leaves=%ld tallgrass=%ld flowers=%ld cactus=%ld sugarcane=%ld\n",
                totals[(int)BlockId::STONE], totals[(int)BlockId::DIRT],
                totals[(int)BlockId::BEDROCK], totals[(int)BlockId::LEAVES] + totals[(int)BlockId::LEAVES_2],
                totals[(int)BlockId::TALL_GRASS],
                totals[(int)BlockId::FLOWER_YELLOW] + totals[(int)BlockId::FLOWER_RED],
                totals[(int)BlockId::CACTUS], totals[(int)BlockId::SUGARCANE]);

    std::printf("biomes:");
    for (auto& kv : biomes) std::printf(" %d=%ld", (int)kv.first, kv.second);
    std::printf("\n");

    bool ok = true;
    if (minH < 1 || maxH > 127) { std::printf("FAIL: height out of range\n"); ok = false; }
    if (totals[(int)BlockId::STONE] == 0) { std::printf("FAIL: no stone\n"); ok = false; }
    if (totals[(int)BlockId::BEDROCK] == 0) { std::printf("FAIL: no bedrock\n"); ok = false; }
    if (ores == 0) { std::printf("FAIL: no ores\n"); ok = false; }
    if (trees == 0) { std::printf("FAIL: no trees\n"); ok = false; }
    if (caves == 0) { std::printf("FAIL: no caves\n"); ok = false; }
    if (water == 0) { std::printf("FAIL: no water\n"); ok = false; }
    if (biomes.size() < 4) { std::printf("FAIL: too few biomes (%zu)\n", biomes.size()); ok = false; }

    std::printf(ok ? "GENERATOR SMOKE: OK\n" : "GENERATOR SMOKE: FAIL\n");
    return ok ? 0 : 1;
}
