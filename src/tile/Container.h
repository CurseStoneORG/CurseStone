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
// Container — interface for tiles that expose an inventory (chest,
// furnace, brewing stand). Mirrors pocketmine/tile/Container.
#pragma once
#include <vector>
#include <cstdint>
namespace cursestone::item {
struct ItemStack;
}
namespace cursestone::tile {
class Container {
  public:
    virtual ~Container() = default;
    virtual int size() const = 0;
    virtual int maxSize() const {
        return 64;
    }
    virtual void setItem(int slot, const cursestone::item::ItemStack& it) = 0;
    virtual cursestone::item::ItemStack getItem(int slot) const = 0;
    virtual void clear() {
        for (int i = 0; i < size(); ++i)
            removeItem(i);
    }
    virtual void removeItem(int slot) = 0;
};
} // namespace
