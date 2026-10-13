/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
#include "PopulatorFlower.h"
#include "PopulatorUtil.h"
#include "world/generator/object/ObjectDecorators.h"
#include "world/generator/biome/BiomeRegistry.h"

namespace cursestone::world::generator::populator {

void PopulatorFlower::populate(GenerationContext& ctx) {
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z,
                                0xF10C0FFEE ^ (uint64_t)flower_);
    for (int i = 0; i < 16; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        const auto& cfg = biome::BiomeRegistry::forId(mcp_chunk_get_biome(ctx.chunk, x, z));
        if (cfg.flowerAmount <= 0) continue;
        if (rng.nextInt(16) >= cfg.flowerAmount) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::flower(ctx.chunk, x, y, z, flower_);
    }
    ctx.chunk->dirty = true;
}
} // namespace cursestone::world::generator::populator
