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
// Item 350. Meta variants: 0 cooked fish, 1 cooked salmon.
class ItemCookedFish : public ItemFood {
  public:
    ItemCookedFish() : ItemFood(350, 5, 6.0f) {
    }
    std::string name() const override {
        return "Cooked Fish";
    }
    int foodPoints(uint8_t meta) const override {
        return meta == 1 ? 6 : 5;
    }
    float foodSaturation(uint8_t meta) const override {
        return meta == 1 ? 9.6f : 6.0f;
    }
};
} // namespace
