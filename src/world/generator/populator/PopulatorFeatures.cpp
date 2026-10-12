#include "PopulatorFeatures.h"
#include "PopulatorUtil.h"
#include "world/generator/TerrainShape.h"
#include "world/generator/object/ObjectDecorators.h"
#include "world/generator/object/LakeGenerator.h"
#include "world/generator/biome/BiomeRegistry.h"
#include "world/generator/biome/Biome.h"
#include <algorithm>

namespace cursestone::world::generator::populator {

namespace {
// Config of the biome under a column (variant ids alias their base).
inline const biome::BiomeConfig& cfgAt(const mcp_chunk_t* c, int x, int z) {
    return biome::BiomeRegistry::forId(mcp_chunk_get_biome(c, x, z));
}

// ---- Lava pools on the plains -------------------------------------------

inline bool inXZ(int v) { return v >= 0 && v < 16; }

// Flat enough for a pool: the ground over a 5x5 window stays level.
bool lavaPoolSiteFlat(const TerrainNoise& noise, int wx, int wz) {
    int lo = 1000, hi = -1000;
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dz = -2; dz <= 2; ++dz) {
            const int h = columnHeight(noise, wx + dx, wz + dz);
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    return (hi - lo) <= 1;
}

// A shallow elliptical puddle of lava sunk into the ground, held in a ring
// of stone. Everything above the pool is cleared so the surface is open.
void carveLavaPool(mcp_chunk_t* c, int cx, int cz, MersenneRandom& rng) {
    const int rx = 2 + (int)rng.nextInt(2);   // 2..3
    const int rz = 2 + (int)rng.nextInt(2);
    const double invx = 1.0 / (double)rx;
    const double invz = 1.0 / (double)rz;
    for (int dx = -rx - 1; dx <= rx + 1; ++dx) {
        for (int dz = -rz - 1; dz <= rz + 1; ++dz) {
            const int x = cx + dx, z = cz + dz;
            if (!inXZ(x) || !inXZ(z)) continue;
            const double nx = (double)dx * invx;
            const double nz = (double)dz * invz;
            const double n = nx * nx + nz * nz;
            const int y = surfaceY(c, x, z);
            if (y < 2) continue;
            if (n <= 1.0) {
                // Interior: one block deep, two in the middle.
                mcp_chunk_set_block_id(c, x, y, z, (uint8_t)block::BlockId::LAVA);
                if (n <= 0.35)
                    mcp_chunk_set_block_id(c, x, y - 1, z, (uint8_t)block::BlockId::LAVA);
                for (int t = y + 1; t <= y + 3; ++t)
                    mcp_chunk_set_block_id(c, x, t, z, (uint8_t)block::BlockId::AIR);
            } else if (n <= 1.5) {
                // Rim: stone, so the lava sits in a visible stone basin.
                mcp_chunk_set_block_id(c, x, y, z, (uint8_t)block::BlockId::STONE);
                for (int t = y + 1; t <= y + 2; ++t)
                    mcp_chunk_set_block_id(c, x, t, z, (uint8_t)block::BlockId::AIR);
            }
        }
    }
}
} // namespace

// Every feature populator works the same way: `attempts_` candidate slots
// per chunk (Normal passes 16), each one accepted only if the biome's
// BiomeConfig amount (out of 16) rolls in. That keeps decoration density
// fully data-driven from biome/<Biome>.cpp.

void PopulatorCactus::populate(GenerationContext& ctx) {
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0xCA07ACE);
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
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x5A6A1CE);
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
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0xDEADB055);
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
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x5A00 ^ (uint64_t)id_);
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
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x9011);
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
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x3E10);
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
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0xD0B1E ^ (uint64_t)data_);
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
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z,
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

void PopulatorLavaPool::populate(GenerationContext& ctx) {
    // A lava pool in the plains has to be an EVENT, so the populator rolls
    // once per chunk and bails out almost every time. The TerrainNoise is
    // only built after the roll, so the rare path is the only one that pays
    // for it.
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x1A7A9007);
    constexpr int kOneIn = 320;
    if (rng.nextInt(kOneIn) != 0) return;

    TerrainNoise noise(ctx.seed);
    const int baseX = ctx.chunk_x * 16;
    const int baseZ = ctx.chunk_z * 16;

    // The roll above decides WHETHER this chunk gets a pool; these candidate
    // columns decide WHERE — the first flat, dry spot in the grassland.
    for (int t = 0; t < 48; ++t) {
        // Keep the pool clear of the chunk border so it is not clipped.
        const int x = 4 + (int)rng.nextInt(8);
        const int z = 4 + (int)rng.nextInt(8);
        const int wx = baseX + x, wz = baseZ + z;

        const int h = columnHeight(noise, wx, wz);
        if (h < kSeaLevel + 2 || h > 105) continue;

        // Only on the flat plains (base or raised variant); a lava pool
        // needs level ground.
        const uint8_t b = columnBiome(noise, wx, wz, h);
        if (b != biome::BIOME_PLAINS && b != biome::BIOME_PLAINS_PLATEAU &&
            b != biome::BIOME_SAVANNA && b != biome::BIOME_SAVANNA_PLATEAU)
            continue;

        // Dry, level ground away from the rivers and the shore.
        if (riverMask(noise, wx, wz) > 0.0 || isShore(noise, wx, wz, h))
            continue;
        if (!lavaPoolSiteFlat(noise, wx, wz)) continue;

        carveLavaPool(ctx.chunk, x, z, rng);
        ctx.chunk->dirty = true;
        return;                     // at most one pool per chunk
    }
}

void PopulatorWaterLily::populate(GenerationContext& ctx) {
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x1117);
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

} // namespace cursestone::world::generator::populator
