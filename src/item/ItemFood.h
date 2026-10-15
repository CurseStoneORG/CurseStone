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
    // food: hunger points restored (half drumsticks). saturation: hidden
    // saturation restored (drains before hunger). always_edible: can be
    // eaten at full hunger. residue_id: item swapped in when the stack
    // empties (bowl for stews, 0 = nothing).
    ItemFood(uint16_t id, int food, float saturation = 0.6f, bool always_edible = false,
             uint16_t residue_id = 0)
        : Item(id), food_(food), saturation_(saturation), always_edible_(always_edible),
          residue_id_(residue_id) {
    }
    bool isFood() const override {
        return true;
    }
    int foodPoints() const override {
        return food_;
    }
    int foodPoints(uint8_t) const override {
        return food_;
    }
    float foodSaturation(uint8_t) const override {
        return saturation_;
    }
    float saturation() const {
        return saturation_;
    }
    bool alwaysEdible() const override {
        return always_edible_;
    }
    uint16_t eatResidueId() const override {
        return residue_id_;
    }

  protected:
    int food_;
    float saturation_;
    bool always_edible_;
    uint16_t residue_id_;
};
} // namespace
