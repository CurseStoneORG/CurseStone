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
#include "PopulatorCave.h"
#include "world/generator/carver/CaveCarver.h"

namespace cursestone::world::generator::populator {
void PopulatorCave::populate(GenerationContext& ctx) {
    // Real vanilla caves (ported CaveCarver). Runs after terrain is filled
    // with stone but before surface decoration, so trees/grass still find
    // solid ground. `attempts_` is kept for API compatibility.
    (void)attempts_;
    carver::CaveCarver cc;
    cc.generate(ctx.seed, ctx.chunk_x, ctx.chunk_z, ctx.chunk);
    ctx.chunk->dirty = true;
}
} // namespace cursestone::world::generator::populator
