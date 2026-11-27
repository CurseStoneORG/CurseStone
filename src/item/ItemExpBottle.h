/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// ItemExpBottle — Bottle o' Enchanting (item id 384, ExperiencePotionItem
// in the 0.15.10 binary). Throwing spawns entity type 68 (type id 0x44,
// ExperiencePotion::getEntityTypeId) which shatters into 3..10 XP split
// into canonical orbs. Vanilla stack size: the binary registers it through
// the plain Item base (no maxStackSize override) — 64, matching Genisys.
#pragma once
#include "Item.h"
#include <string>
namespace cursestone::item {
class ItemExpBottle : public Item {
  public:
    ItemExpBottle() : Item(384) {
    }
    std::string name() const override {
        return "Bottle o' Enchanting";
    }
};
} // namespace
