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
// Inventory.h — ported from cn.nukkit.inventory.Inventory (interface)
//
// Abstract inventory interface. Concrete implementations:
//   - BaseInventory   (the default in-memory storage)
//   - PlayerInventory (extends BaseInventory with hotbar + armor + held slot)
//
// In Nukkit this is a Java interface with ~30 methods. We mirror the
// public surface but adapt signatures to C++ (std::vector instead of
// Item[], shared_ptr<Player> instead of Player, etc.).
#pragma once
#include "item/ItemStack.h"
#include "InventoryType.h"
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <cstdint>

namespace cursestone {

class Player; // forward

namespace inventory {

class Inventory {
  public:
    static constexpr int MAX_STACK = 64;

    virtual ~Inventory() = default;

    virtual int getSize() const = 0;
    virtual int getMaxStackSize() const = 0;
    virtual void setMaxStackSize(int size) = 0;

    virtual const std::string& getName() const = 0;
    virtual const std::string& getTitle() const = 0;

    virtual item::ItemStack getItem(int index) = 0;
    virtual bool setItem(int index, const item::ItemStack& item) = 0;

    // addItem returns the leftovers (items that didn't fit). If the
    // returned vector is empty, everything was added.
    virtual std::vector<item::ItemStack> addItem(std::vector<item::ItemStack> slots) = 0;
    virtual bool canAddItem(const item::ItemStack& item) = 0;
    virtual std::vector<item::ItemStack> removeItem(std::vector<item::ItemStack> slots) = 0;

    // Contents as a map (slot index → item). Only non-empty slots are
    // included, matching Nukkit's HashMap<Integer,Item> slots semantics.
    virtual std::unordered_map<int, item::ItemStack> getContents() = 0;
    virtual void setContents(const std::unordered_map<int, item::ItemStack>& items) = 0;

    // Send the full contents or a single slot to a player. These
    // generate ContainerSetContentPacket / ContainerSetSlotPacket.
    // Mirrors Nukkit's sendContents(player) / sendSlot(index, player).
    virtual void sendContents(Player* player) = 0;
    virtual void sendSlot(int index, Player* player) = 0;

    virtual bool contains(const item::ItemStack& item) = 0;
    virtual std::unordered_map<int, item::ItemStack> all(const item::ItemStack& item) = 0;
    virtual int first(const item::ItemStack& item) = 0;
    virtual int firstEmpty(const item::ItemStack& item) = 0;
    virtual void remove(const item::ItemStack& item) = 0;

    virtual bool clear(int index) = 0;
    virtual void clearAll() = 0;

    // onSlotChange is called after a slot is modified. The default
    // implementation sends a ContainerSetSlotPacket to all viewers.
    virtual void onSlotChange(int index, const item::ItemStack& before) = 0;

    virtual std::shared_ptr<InventoryType> getType() = 0;
};

} // namespace inventory
} // namespace cursestone
