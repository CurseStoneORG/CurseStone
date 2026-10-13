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
// EmptyGenerator — void world. Generates bedrock at y=0 only.
#pragma once
#include "Generator.h"
namespace cursestone::world::generator {
class EmptyGenerator : public Generator {
public:
    const char* name() const override { return "void"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext&) override {}
    // Void spawn — on top of the bedrock floor.
    mcp_vector3_t getSpawn() const override {
        return mcp_v3(128.0, 2.0, 128.0);
    }
};
} // namespace
