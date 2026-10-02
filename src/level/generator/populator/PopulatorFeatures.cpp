#include "PopulatorFeatures.h"
#include "PopulatorUtil.h"
#include "level/generator/object/ObjectDecorators.h"
#include "level/generator/object/LakeGenerator.h"
#include "level/generator/biome/BiomeRegistry.h"

namespace cursestone::level::generator::populator {

namespace {
// Config of the biome under a column (variant ids alias their base).
inline const biome::BiomeConfig& cfgAt(const mcp_chunk_t* c, int x, int z) {
    return biome::BiomeRegistry::forId(mcp_chunk_get_biome(c, x, z));
}
} // namespace

// Every feature populator works the same way: `attempts_` candidate slots
// per chunk (Normal passes 16), each one accepted only if the biome's
// BiomeConfig amount (out of 16) rolls in. That keeps decoration density
// fully data-driven from biome/<Biome>.cpp.

void PopulatorCactus::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0xCA07ACE);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        if (cfg.cactusAmount <= 0 || rng.nextInt(16) >= cfg.cactusAmount) continue;
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
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        if (cfg.sugarCaneAmount <= 0 || rng.nextInt(16) >= cfg.sugarCaneAmount) continue;
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
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        if (cfg.deadBushAmount <= 0 || rng.nextInt(16) >= cfg.deadBushAmount) continue;
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
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        int amount = cfg.mushroomAmount;
        if (amount <= 0) continue;
        // Red mushrooms are half as common as brown ones.
        if (id_ == (uint8_t)block::BlockId::RED_MUSHROOM) amount = (amount + 1) / 2;
        if (rng.nextInt(16) >= amount) continue;
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
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        if (cfg.pumpkinAmount <= 0 || rng.nextInt(16) >= cfg.pumpkinAmount) continue;
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
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        if (cfg.melonAmount <= 0 || rng.nextInt(16) >= cfg.melonAmount) continue;
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
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        if (cfg.doublePlantAmount <= 0 || rng.nextInt(16) >= cfg.doublePlantAmount) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::doublePlant(ctx.chunk, x, y, z, data_);
    }
    ctx.chunk->dirty = true;
}

void PopulatorLake::populate(GenerationContext& ctx) {
    // Lakes are terrain-scale features, not biome decoration: vanilla
    // rolls them per chunk regardless of the biome underneath.
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
        const auto& cfg = cfgAt(ctx.chunk, x, z);
        if (cfg.waterLilyAmount <= 0 || rng.nextInt(16) >= cfg.waterLilyAmount) continue;
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
