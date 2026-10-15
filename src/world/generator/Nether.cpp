/*
 _____                     _____ _
/  __ \                   /  ___| |
| |   | | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#include "Nether.h"
#include "world/format/Chunk.h"
#include "world/generator/object/OreGenerator.h"
#include "block/BlockIds.h"
#include <algorithm>
#include <cmath>

namespace cursestone::world::generator {
namespace {
using namespace cursestone::block;

// Classic Nether profile at the 0.15.x world height.
constexpr int    kHeight        = 128;
constexpr int    kCavernCentre  = 64;   // open belt sits around this Y
constexpr int    kLavaLevel     = 32;   // still lava fills open space <= this
constexpr double kDensityOffset = 0.5;  // shifts the solid/air threshold

// Sparse 3D noise sampling (fast field): evaluate the noise every 4
// blocks in X/Z and every 8 in Y, then trilinear-interpolate each block.
constexpr int kStepXZ    = 4;
constexpr int kStepY     = 8;
constexpr int kSamplesXZ = 16 / kStepXZ + 1;   // 5
constexpr int kSamplesY  = kHeight / kStepY + 1;   // 17

// Trilinear sample of the sparse field at chunk-local (x, y, z).
float fieldAt(const float field[kSamplesY][kSamplesXZ][kSamplesXZ],
              int x, int y, int z) {
    int gx = x >> 2, gy = y >> 3, gz = z >> 2;
    float fx = (x & 3) / (float)kStepXZ;
    float fy = (y & 7) / (float)kStepY;
    float fz = (z & 3) / (float)kStepXZ;
    float c00 = field[gy][gx][gz]     + (field[gy][gx + 1][gz]     - field[gy][gx][gz])     * fx;
    float c10 = field[gy][gx][gz + 1] + (field[gy][gx + 1][gz + 1] - field[gy][gx][gz + 1]) * fx;
    float c01 = field[gy + 1][gx][gz]     + (field[gy + 1][gx + 1][gz]     - field[gy + 1][gx][gz])     * fx;
    float c11 = field[gy + 1][gx][gz + 1] + (field[gy + 1][gx + 1][gz + 1] - field[gy + 1][gx][gz + 1]) * fx;
    float c0  = c00 + (c10 - c00) * fz;
    float c1  = c01 + (c11 - c01) * fz;
    return c0 + (c1 - c0) * fy;
}

// Deterministic per-chunk random stream shared by both phases.
int64_t chunkStream(int64_t seed, int32_t cx, int32_t cz) {
    return (int64_t)0xdeadbeefULL ^ ((int64_t)cx << 8) ^ (int64_t)cz ^ seed;
}

// Jagged bedrock shell, vanilla distribution: solid at the outer face,
// then 4 layers with a decreasing chance of bedrock towards the inside.
void placeBedrockShell(mcp_chunk_t* c, MersenneRandom& rng, int x, int z) {
    for (int y = 0; y <= 4; ++y) {
        if (y == 0 || rng.nextDouble() >= y / 4.0)
            mcp_chunk_set_block_id(c, x, y, z, BEDROCK);
    }
    for (int y = kHeight - 5; y < kHeight; ++y) {
        if (y == kHeight - 1 || rng.nextDouble() >= (kHeight - 1 - y) / 4.0)
            mcp_chunk_set_block_id(c, x, y, z, BEDROCK);
    }
}

// Small glowstone cluster hanging under a netherrack ceiling: walks down
// and sideways from the air cell below the ceiling, filling air only.
void placeGlowstone(mcp_chunk_t* c, MersenneRandom& rng) {
    int x = rng.nextInt(16), z = rng.nextInt(16);
    int ceiling = -1;
    for (int y = kHeight - 2; y >= kLavaLevel; --y) {
        if (mcp_chunk_get_block_id(c, x, y, z) == NETHERRACK) { ceiling = y; break; }
    }
    if (ceiling < 0) return;

    int blocks = 3 + rng.nextInt(6);   // 3..8 glowstone blocks
    int cx = x, cy = ceiling - 1, cz = z;
    for (int i = 0; i < blocks * 3 && blocks > 0; ++i) {
        if (cy < kLavaLevel) break;
        if (mcp_chunk_get_block_id(c, cx, cy, cz) == AIR) {
            mcp_chunk_set_block_id(c, cx, cy, cz, GLOWSTONE);
            --blocks;
            --cy;                          // bias downwards (hanging)
        } else {
            cx = std::max(0, std::min(15, cx + rng.nextInt(3) - 1));
            cz = std::max(0, std::min(15, cz + rng.nextInt(3) - 1));
            if (rng.nextBoolean()) --cy;
        }
    }
}

// Ground fire: a flame resting on netherrack (eternal fire, vanilla).
void placeGroundFire(mcp_chunk_t* c, MersenneRandom& rng) {
    int x = rng.nextInt(16), z = rng.nextInt(16);
    for (int y = 1; y < kHeight - 1; ++y) {
        if (mcp_chunk_get_block_id(c, x, y, z) != AIR) continue;
        if (mcp_chunk_get_block_id(c, x, y - 1, z) == NETHERRACK)
            mcp_chunk_set_block_id(c, x, y, z, FIRE);
        return;
    }
}
} // namespace

Nether::Nether(int64_t seed)
    : random_((long)seed), noise_(&random_, 4), seed_(seed) {}

Nether::~Nether() = default;

void Nether::generate(GenerationContext& ctx) {
    mcp_chunk_t* c = ctx.chunk;

    // Sparse density samples in world coordinates (64-block wavelength).
    float field[kSamplesY][kSamplesXZ][kSamplesXZ];
    for (int sy = 0; sy < kSamplesY; ++sy)
        for (int sx = 0; sx < kSamplesXZ; ++sx)
            for (int sz = 0; sz < kSamplesXZ; ++sz)
                field[sy][sx][sz] = (float)noiseValue3d(
                    noise_,
                    (ctx.chunk_x * 16 + sx * kStepXZ) / 64.0,
                    (sy * kStepY) / 64.0,
                    (ctx.chunk_z * 16 + sz * kStepXZ) / 64.0);

    // Terrain columns: netherrack where the density field is solid, open
    // space elsewhere, lava sea below kLavaLevel. HELL biome everywhere.
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z) {
            mcp_chunk_set_biome(c, x, z, 8);   // HELL
            for (int y = 0; y < kHeight; ++y) {
                double n = fieldAt(field, x, y, z);
                double d = std::fabs(kCavernCentre - y) / (double)kCavernCentre
                           - n - kDensityOffset;
                if (d > 0.0)
                    mcp_chunk_set_block_id(c, x, y, z, NETHERRACK);
                else if (y <= kLavaLevel)
                    mcp_chunk_set_block_id(c, x, y, z, LAVA);
            }
        }

    // Bedrock shell with the natural jagged distribution.
    MersenneRandom rng((long)chunkStream(ctx.seed, ctx.chunk_x, ctx.chunk_z));
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            placeBedrockShell(c, rng, x, z);
}

void Nether::populate(GenerationContext& ctx) {
    mcp_chunk_t* c = ctx.chunk;

    // Ore-style features: random-walk veins replacing netherrack only.
    // Each vein gets its own seed offset: the per-vein seed is truncated
    // to 32 bits inside the random, so two veins of the same shape would
    // otherwise retrace the exact same cells and the later one would
    // place nothing.
    using object::OreVein;
    const int64_t seed = ctx.seed;
    object::OreGenerator::place(c, seed,               {NETHER_QUARTZ_ORE, NETHERRACK, 4, 123, 16, 20});
    object::OreGenerator::place(c, seed ^ 0x51ed2701LL, {SOUL_SAND,        NETHERRACK, 4, 123, 64,  5});
    object::OreGenerator::place(c, seed ^ 0x2545f491LL, {GRAVEL,           NETHERRACK, 4, 123, 64,  5});
    object::OreGenerator::place(c, seed ^ 0x7f4a7c15LL, {LAVA,             NETHERRACK, 4, kLavaLevel, 16, 1});

    MersenneRandom rng((long)chunkStream(ctx.seed, ctx.chunk_x, ctx.chunk_z));
    placeGlowstone(c, rng);
    placeGlowstone(c, rng);
    placeGroundFire(c, rng);
    if (rng.nextBoolean()) placeGroundFire(c, rng);
}
} // namespace cursestone::world::generator
