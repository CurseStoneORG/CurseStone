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
#include "Item.h"
#include <vector>
namespace cursestone::item {
std::vector<std::unique_ptr<Item>>& Item::registry() {
    static std::vector<std::unique_ptr<Item>> r;
    if (r.empty())
        r.resize(65536);
    return r;
}
void Item::registerItem(std::unique_ptr<Item> b) {
    uint16_t id = b->id();
    registry()[id] = std::move(b);
}
Item* Item::get(uint16_t id) {
    auto& r = registry();
    if (id >= r.size())
        return nullptr;
    return r[id].get();
}
} // namespace
