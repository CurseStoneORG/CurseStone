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
#include <cstdint>
namespace cursestone::item {
enum ArmorMaterial : int {
    ARMOR_LEATHER = 0, ARMOR_CHAIN = 1, ARMOR_IRON = 2, ARMOR_GOLD = 3, ARMOR_DIAMOND = 4,
};
enum ArmorSlot : int {
    HELMET = 0, CHESTPLATE = 1, LEGGINGS = 2, BOOTS = 3,
};
class ItemArmor : public Item {
public:
    ItemArmor(uint16_t id, ArmorMaterial m, ArmorSlot s, int defense)
        : Item(id), mat_(m), slot_(s), defense_(defense) {}
    bool isArmor() const override { return true; }
    int maxStackSize() const override { return 1; }
    ArmorMaterial material() const { return mat_; }
    ArmorSlot slot() const { return slot_; }
    int defensePoints() const { return defense_; }
private:
    ArmorMaterial mat_;
    ArmorSlot slot_;
    int defense_;
};
} // namespace
