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
// Nether — vanilla-style Nether generator for the 0.15.x world height
// (128 blocks, no biomes beyond HELL):
//
//   * bedrock floor (solid at y=0, jagged up to y=4) and bedrock roof
//     (solid at y=127, jagged down to y=123), the natural vanilla
//     distribution,
//   * netherrack terrain shaped by 3D density noise: a mostly solid
//     shell at both ends and a large cavern belt around y=64,
//   * a still-lava sea filling the open space up to y=32,
//   * features: quartz ore, soul sand and gravel patches, hidden lava
//     pockets, glowstone clusters hanging under ceilings, ground fire.
//
// The noise is the shared vanilla module (noise/): one MersenneRandom
// feeding a single 4-octave PerlinNoise evaluated in 3D, sampled
// sparsely and trilinear-interpolated like the classic fast 3D field.
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
    // Must be declared first: the noise layer holds a pointer to it.
    MersenneRandom random_;
    PerlinNoise noise_;
    int64_t seed_;
};
} // namespace
