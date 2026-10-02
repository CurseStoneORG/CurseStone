#include "PopulatorFeatures.h"
#include "PopulatorUtil.h"
#include "level/generator/object/ObjectDecorators.h"
#include "level/generator/object/LakeGenerator.h"
#include "level/generator/biome/Biome.h"
using namespace cursestone::level::generator::biome;

namespace cursestone::level::generator::populator {

void PopulatorCactus::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0xCA07ACE);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t b = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (b != BIOME_DESERT && b != BIOME_DESERT_HILLS) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::cactus(ctx.chunk, x, y, z, rng);
    }
    ctx.chunk->dirty = true;
}

void PopulatorSugarCane::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x5A6A1CE);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::sugarCane(ctx.chunk, x, y, z, rng);
    }
    ctx.chunk->dirty = true;
}

void PopulatorDeadBush::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0xDEADB055);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t b = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (b != BIOME_DESERT && b != BIOME_DESERT_HILLS) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::deadBush(ctx.chunk, x, y, z);
    }
    ctx.chunk->dirty = true;
}

void PopulatorMushroom::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x5A00 ^ (uint64_t)id_);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t b = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (b != BIOME_FOREST && b != BIOME_FOREST_HILLS &&
            b != BIOME_TAIGA && b != BIOME_TAIGA_HILLS &&
            b != BIOME_SWAMPLAND) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::mushroom(ctx.chunk, x, y, z, id_);
    }
    ctx.chunk->dirty = true;
}

void PopulatorPumpkin::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x9011);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t b = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (b != BIOME_PLAINS && b != BIOME_FOREST && b != BIOME_FOREST_HILLS) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::pumpkin(ctx.chunk, x, y, z, rng);
    }
    ctx.chunk->dirty = true;
}

void PopulatorMelon::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x3E10);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t b = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (b != BIOME_JUNGLE && b != BIOME_JUNGLE_HILLS) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::melon(ctx.chunk, x, y, z);
    }
    ctx.chunk->dirty = true;
}

void PopulatorDoublePlant::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0xD0B1E ^ (uint64_t)data_);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t b = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (!biomeHasVegetation(b)) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::doublePlant(ctx.chunk, x, y, z, data_);
    }
    ctx.chunk->dirty = true;
}

void PopulatorLake::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z,
                                0x1AEE ^ (uint64_t)liquid_);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        int y = (liquid_ == 11) ? (5 + (int)rng.nextInt(20))
                                : (60 + (int)rng.nextInt(15));
        object::LakeGenerator::place(ctx.chunk, x, y, z, liquid_, rng);
    }
    ctx.chunk->dirty = true;
}

void PopulatorWaterLily::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x1117);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        // Find the water surface in this column.
        for (int y = 127; y > 1; --y) {
            uint8_t id = mcp_chunk_get_block_id(ctx.chunk, x, y, z);
            if (id == (uint8_t)block::BlockId::WATER ||
                id == (uint8_t)block::BlockId::FLOWING_WATER) {
                if (mcp_chunk_get_block_id(ctx.chunk, x, y + 1, z) == 0)
                    object::ObjectDecorators::waterLily(ctx.chunk, x, y, z);
                break;
            }
            if (id != 0) break;
        }
    }
    ctx.chunk->dirty = true;
}

} // namespace cursestone::level::generator::populator
