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
#include "ItemFood.h"
#include <string>
namespace cursestone::item {
// Item 349. Meta variants: 0 raw fish, 1 salmon, 2 clownfish, 3 pufferfish.
class ItemFish : public ItemFood {
  public:
    ItemFish() : ItemFood(349, 2, 0.4f) {
    }
    std::string name() const override {
        return "Fish";
    }
    int foodPoints(uint8_t meta) const override {
        return (meta == 2 || meta == 3) ? 1 : 2;
    }
    float foodSaturation(uint8_t meta) const override {
        return (meta == 2 || meta == 3) ? 0.2f : 0.4f;
    }
};
} // namespace
