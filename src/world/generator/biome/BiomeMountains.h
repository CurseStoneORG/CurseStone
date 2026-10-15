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
// BiomeMountains — bioma de montaña (extreme hills).
#pragma once
#include "Biome.h"
namespace cursestone::world::generator::biome {
class BiomeMountains : public Biome {
  public:
    uint8_t id() const override;
    const char* name() const override {
        return "mountains";
    }
    float temperature() const override {
        return 0.2f;
    }
    float rainfall() const override {
        return 0.3f;
    }
    // Sin capa de tierra: la roca llega hasta la superficie.
    uint8_t topBlock() const override {
        return 1;
    } // stone
    uint8_t fillerBlock() const override {
        return 1;
    } // stone
};
} // namespace
