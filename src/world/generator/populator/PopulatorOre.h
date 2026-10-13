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
// PopulatorOre — places ore veins using OreGenerator.
#pragma once
#include "Populator.h"
#include "world/generator/object/OreGenerator.h"
#include <vector>
namespace cursestone::world::generator::populator {
class PopulatorOre : public Populator {
public:
    explicit PopulatorOre(std::vector<object::OreVein> veins) : veins_(std::move(veins)) {}
    const char* name() const override { return "ore"; }
    void populate(GenerationContext& ctx) override;
private:
    std::vector<object::OreVein> veins_;
};
} // namespace
