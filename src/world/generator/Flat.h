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
// Flat — super-flat generator. Layers from y=0 upward are defined
// by a layer list in the constructor. Default layer profile is
// bedrock / 2x dirt / grass (y=4 = grass), matching the reference server's
// default flat preset.
#pragma once
#include "Generator.h"
#include <vector>
#include <cstdint>
namespace cursestone::world::generator {
struct FlatLayer {
    uint8_t block_id;
    uint8_t count;
};
class Flat : public Generator {
  public:
    explicit Flat(std::vector<FlatLayer> layers) : layers_(std::move(layers)) {
    }
    const char* name() const override {
        return "flat";
    }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext&) override {
    } // flat = no features
    // Mirrors Nukkit Flat.getSpawn(): (128, floorLevel, 128)
    // floorLevel = sum of all layer counts (y where grass is)
    mcp_vector3_t getSpawn() const override {
        int floor = 0;
        for (const auto& l : layers_)
            floor += l.count;
        return mcp_v3(128.0, (double)floor, 128.0);
    }

  private:
    std::vector<FlatLayer> layers_;
};
} // namespace
