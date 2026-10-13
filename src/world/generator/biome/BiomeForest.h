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
// BiomeForest — bioma de bosque.
#pragma once
#include "Biome.h"
namespace cursestone::world::generator::biome {
class BiomeForest : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "forest"; }
    float temperature() const override { return 0.7f; }
    float rainfall()    const override { return 0.8f; }
    uint8_t topBlock()   const override { return 2; } // grass
    uint8_t fillerBlock() const override { return 3; } // dirt
};
} // namespace
