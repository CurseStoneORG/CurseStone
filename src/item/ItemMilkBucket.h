/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| | | | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
#include "ItemConstants.h"
#include "ItemFood.h"
#include "ItemIds.h"
#include <string>
namespace cursestone::item {
// Drinking milk clears every status effect and leaves an empty bucket.
class ItemMilkBucket : public ItemFood {
  public:
    ItemMilkBucket()
        : ItemFood(ITEM_MILK_BUCKET, 0 /* no hunger */, 0.0f /* no saturation */,
                   true /* drinkable at any hunger */, BUCKET /* empty bucket */) {
    }
    std::string name() const override {
        return "Milk Bucket";
    }
    // Buckets never stack.
    int maxStackSize() const override {
        return 1;
    }
    // Removes every active status effect from the drinker.
    void applyEatEffects(Player& eater, uint8_t meta) const override;
};
} // namespace cursestone::item
