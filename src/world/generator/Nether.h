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
// Nether — nether generator. Fills the chunk with netherrack
// (block 87) up to y=128 and carves caves heavily with a high
// frequency 3D noise. Lava lakes at low Y. No biomes.
//
// El ruido es el mismo modulo vanilla del Overworld (noise/): una sola
// fuente (MersenneRandom) y dos capas de PerlinNoise, una para el relieve y
// otra para las cuevas. Antes usaba el SimplexNoise propio, que ya no existe.
#pragma once
#include "Generator.h"
#include "world/generator/noise/VanillaNoise.h"
namespace cursestone::world::generator {
class Nether : public Generator {
public:
    explicit Nether(int64_t seed);
    ~Nether() override;
    const char* name() const override { return "nether"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext& ctx) override;
    // Mirrors Nukkit Nether.getSpawn(): (128, 64, 128)
    mcp_vector3_t getSpawn() const override {
        return mcp_v3(128.0, 64.0, 128.0);
    }
private:
    // Tiene que ir primero: las dos capas toman &random_.
    MersenneRandom random_;
    PerlinNoise terrain_;
    PerlinNoise cave_;
    int64_t seed_;
};
} // namespace
