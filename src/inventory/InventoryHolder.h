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
// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryHolder.h — ported from cn.nukkit.inventory.InventoryHolder
//
// Anything that "holds" an inventory (Player, Chest tile, Furnace tile)
// implements this interface so the inventory can ask its holder for
// context (e.g. who to send packets to, where to spawn the container).
#pragma once
#include <memory>
namespace cursestone::inventory {

class Inventory;

class InventoryHolder {
  public:
    virtual ~InventoryHolder() = default;
    // Returns the inventory this holder owns. May return nullptr if the
    // holder has been closed/destroyed.
    virtual std::shared_ptr<Inventory> getInventory() = 0;
};

} // namespace cursestone::inventory
