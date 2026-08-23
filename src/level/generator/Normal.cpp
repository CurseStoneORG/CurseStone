#include "Normal.h"
#include "level/format/Chunk.h"
#include "level/generator/populator/PopulatorOre.h"
#include "level/generator/populator/PopulatorTree.h"
#include "level/generator/populator/PopulatorTallGrass.h"
#include "level/generator/populator/PopulatorFlower.h"
#include "level/generator/populator/PopulatorCave.h"
#include "level/generator/object/OreGenerator.h"
#include <cmath>
namespace cenisyspe::level::generator {
Normal::Normal(int64_t seed) : seed_(seed) {
    height_noise_ = mcp_simplex_new(seed);
    cave_noise_   = mcp_simplex_new(seed ^ 0xC0FFEE);
    temp_noise_   = mcp_simplex_new(seed ^ 0xBA5EBA11);
    using object::OreVein;
    std::vector<OreVein> ores = {
        {14, 0, 32, 8, 4},    // gold
        {15, 0, 64, 9, 2},    // iron
        {16, 0, 16, 12, 3},   // coal
        {56, 0, 16, 6, 12},   // diamond
        {73, 0, 24, 6, 6},    // redstone
        {21, 0, 24, 6, 4},    // lapis
    };
    populators_.push_back(std::make_unique<populator::PopulatorOre>(std::move(ores)));
    populators_.push_back(std::make_unique<populator::PopulatorTree>(4, object::TREE_OAK));
    populators_.push_back(std::make_unique<populator::PopulatorTallGrass>(8));
    populators_.push_back(std::make_unique<populator::PopulatorFlower>(37, 4)); // dandelion
    populators_.push_back(std::make_unique<populator::PopulatorFlower>(38, 2)); // poppy
    populators_.push_back(std::make_unique<populator::PopulatorCave>(2));
}
Normal::~Normal() {
    mcp_simplex_destroy(height_noise_);
    mcp_simplex_destroy(cave_noise_);
    mcp_simplex_destroy(temp_noise_);
}
void Normal::generate(GenerationContext& ctx) {
    mcp_chunk_t* c = ctx.chunk;
    auto biome_at = [&](int cx, int cz) -> uint8_t {
        double t = mcp_simplex_noise2d(temp_noise_, (ctx.chunk_x * 16 + cx) * 0.01, (ctx.chunk_z * 16 + cz) * 0.01);
        if (t < -0.4) return 2; // desert
        if (t > 0.5)  return 4; // mountains
        if (t < -0.2) return 1; // plains
        return 3;               // forest
    };
    for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) mcp_chunk_set_biome(c, x, z, biome_at(x, z));

    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            double wx = (ctx.chunk_x * 16 + x) * 0.0125;
            double wz = (ctx.chunk_z * 16 + z) * 0.0125;
            double n = mcp_simplex_fbm2d(height_noise_, wx, wz, 4, 0.5, 2.0);
            int h = 64 + (int)((n + 1.0) * 18.0);  // 28..100
            uint8_t top = 2, dirt = 3, biome = biome_at(x, z);
            if (biome == 2) { top = 12; dirt = 12; }    // desert: sand
            if (biome == 4) { top = 1;  dirt = 3;  }    // mountains: stone tops
            for (int y = 0; y < h; ++y) {
                uint8_t id;
                if (y == 0)            id = 7;          // bedrock
                else if (y < h - 4)    id = 1;          // stone
                else if (y < h - 1)   id = dirt;
                else                  id = top;
                mcp_chunk_set_block_id(c, x, y, z, id);
            }
            for (int y = h; y < 64; ++y) {
                if (mcp_chunk_get_block_id(c, x, y, z) == 0)
                    mcp_chunk_set_block_id(c, x, y, z, 8); // water
            }
            mcp_chunk_set_height_map(c, x, z, h);
        }
    }
    c->terrain_populated = true;
    c->light_populated = true;
    c->dirty = true;
}
void Normal::populate(GenerationContext& ctx) {
    for (auto& p : populators_) p->populate(ctx);
}
} // namespace
