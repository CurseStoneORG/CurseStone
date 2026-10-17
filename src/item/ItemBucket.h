/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| | | | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
#include "ItemConstants.h"
#include "Item.h"
#include <string>
namespace cursestone::item {
// Buckets share one item id on the 0.15 wire: aux 0 is the empty
// bucket, 8 water, 10 lava and 1 MILK (the creative catalog hands out
// 325 with damage 1 — the standalone 335 id never reaches this
// protocol). Only the milk variant is edible: drinking it strips every
// status effect and swaps the stack for an empty bucket.
class ItemBucket : public Item {
  public:
    // Aux value that marks the milk variant of the bucket.
    static constexpr uint8_t META_MILK = 1;
    ItemBucket() : Item(BUCKET) {
    }
    std::string name() const override {
        return "Bucket";
    }
    // Only the milk variant is drinkable.
    bool isFood(uint8_t meta) const override {
        return meta == META_MILK;
    }
    // Milk restores no hunger, so it must be drinkable at full hunger.
    bool alwaysEdible(uint8_t meta) const override {
        return meta == META_MILK;
    }
    int foodPoints(uint8_t meta) const override {
        (void)meta; // milk restores no hunger either way
        return 0;
    }
    float foodSaturation(uint8_t meta) const override {
        (void)meta;
        return 0.0f;
    }
    // Drinking the milk variant always leaves an empty bucket.
    uint16_t eatResidueId(uint8_t meta) const override {
        return meta == META_MILK ? (uint16_t)BUCKET : 0;
    }
    // Removes every active status effect from the drinker.
    void applyEatEffects(Player& eater, uint8_t meta) const override;
};
} // namespace
