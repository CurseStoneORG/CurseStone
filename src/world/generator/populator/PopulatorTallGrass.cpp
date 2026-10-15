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
#include "PopulatorTallGrass.h"
#include "PopulatorUtil.h"
#include "world/generator/object/ObjectDecorators.h"
#include "world/generator/biome/BiomeRegistry.h"
#include "world/generator/biome/Biome.h"
using namespace cursestone::world::generator::biome;

namespace cursestone::world::generator::populator {

namespace {
// Taiga/ice/hills grow ferns, everything else tall grass.
bool wantsFern(uint8_t b) {
    return b == BIOME_TAIGA || b == BIOME_TAIGA_HILLS || b == BIOME_ICE_PLAINS ||
           b == BIOME_ICE_MOUNTAINS || b == BIOME_EXTREME_HILLS;
}
} // namespace

void PopulatorTallGrass::populate(GenerationContext& ctx) {
    MersenneRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x67726173);
    for (int i = 0; i < 16; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t biome = mcp_chunk_get_biome(ctx.chunk, x, z);
        const auto& cfg = BiomeRegistry::forId(biome);
        if (cfg.grassAmount <= 0)
            continue;
        if (rng.nextInt(16) >= cfg.grassAmount)
            continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0)
            continue;
        object::ObjectDecorators::tallGrass(ctx.chunk, x, y, z, wantsFern(biome) ? 2 : 1);
    }
    ctx.chunk->dirty = true;
}
} // namespace cursestone::world::generator::populator
