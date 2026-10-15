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
#include "Item.h"
namespace cursestone::item {
class ItemFood : public Item {
  public:
    ItemFood(uint16_t id, int food, float saturation = 0.6f)
        : Item(id), food_(food), saturation_(saturation) {
    }
    bool isFood() const override {
        return true;
    }
    int foodPoints() const override {
        return food_;
    }
    float saturation() const {
        return saturation_;
    }

  private:
    int food_;
    float saturation_;
};
} // namespace
