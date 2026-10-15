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
// EnchantmentList — list of (enchantment id + level) pairs attached
// to an item. Stored in NBT as "ench" tag list.
#pragma once
#include "Enchantment.h"
#include <vector>
namespace cursestone::item::enchantment {
struct EnchantmentEntry {
    EnchantmentId id;
    uint8_t level;
};
class EnchantmentList {
  public:
    void add(EnchantmentId id, uint8_t level) {
        for (auto& e : entries_)
            if (e.id == id) {
                e.level = level;
                return;
            }
        entries_.push_back({id, level});
    }
    bool has(EnchantmentId id) const {
        for (const auto& e : entries_)
            if (e.id == id)
                return true;
        return false;
    }
    uint8_t level(EnchantmentId id) const {
        for (const auto& e : entries_)
            if (e.id == id)
                return e.level;
        return 0;
    }
    const std::vector<EnchantmentEntry>& entries() const {
        return entries_;
    }

  private:
    std::vector<EnchantmentEntry> entries_;
};
} // namespace
