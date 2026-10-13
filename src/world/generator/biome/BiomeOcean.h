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
// BiomeOcean — bioma oceano.
#pragma once
#include "Biome.h"
namespace cursestone::world::generator::biome {
class BiomeOcean : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "ocean"; }
    float temperature() const override { return 0.5f; }
    float rainfall()    const override { return 0.5f; }
    // Fondo de grava: el oceano es lo bastante profundo como para que no
    // haya capa de tierra debajo de la arena.
    uint8_t topBlock()   const override { return 13; } // gravel
    uint8_t fillerBlock() const override { return 13; } // gravel
};
} // namespace
