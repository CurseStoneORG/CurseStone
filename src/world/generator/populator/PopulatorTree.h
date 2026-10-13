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
#pragma once
#include "Populator.h"
#include "world/generator/object/TreeGenerator.h"
namespace cursestone::world::generator::populator {
// Scatters trees, choosing the vanilla species for the biome under each
// candidate column (oak/birch in forests, spruce in taiga/hills, jungle in
// jungle, acacia in savanna, swamp oak in swampland...).
class PopulatorTree : public Populator {
public:
    explicit PopulatorTree(int attempts_per_chunk) : attempts_(attempts_per_chunk) {}
    const char* name() const override { return "tree"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};
} // namespace
