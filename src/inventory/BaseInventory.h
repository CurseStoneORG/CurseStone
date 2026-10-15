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
// BaseInventory.h — ported from cn.nukkit.inventory.BaseInventory
//
// Concrete base class implementing the Inventory interface. Stores
// slots in a HashMap-like structure, supports add/remove/canAdd, and
// sends slot-change packets to viewers.
//
// PlayerInventory extends this with hotbar + armor + held-slot logic.
#pragma once
#include "Inventory.h"
#include "InventoryHolder.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
namespace cursestone::inventory {

class BaseInventory : public Inventory {
  public:
    BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type);
    BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type,
                  const std::unordered_map<int, item::ItemStack>& items);
    BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type,
                  const std::unordered_map<int, item::ItemStack>& items, int overrideSize);
    BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type,
                  const std::unordered_map<int, item::ItemStack>& items, int overrideSize,
                  const std::string& overrideTitle);

    ~BaseInventory() override = default;

    // ---- Inventory interface ----
    int getSize() const override {
        return size_;
    }
    void setSize(int sz) {
        size_ = sz;
    }
    int getMaxStackSize() const override {
        return maxStackSize_;
    }
    void setMaxStackSize(int sz) override {
        maxStackSize_ = sz;
    }
    const std::string& getName() const override {
        return name_;
    }
    const std::string& getTitle() const override {
        return title_;
    }

    item::ItemStack getItem(int index) override;
    bool setItem(int index, const item::ItemStack& item) override;

    std::vector<item::ItemStack> addItem(std::vector<item::ItemStack> slots) override;
    bool canAddItem(const item::ItemStack& item) override;
    std::vector<item::ItemStack> removeItem(std::vector<item::ItemStack> slots) override;

    std::unordered_map<int, item::ItemStack> getContents() override;
    void setContents(const std::unordered_map<int, item::ItemStack>& items) override;

    void sendContents(Player* player) override;
    void sendSlot(int index, Player* player) override;

    bool contains(const item::ItemStack& item) override;
    std::unordered_map<int, item::ItemStack> all(const item::ItemStack& item) override;
    int first(const item::ItemStack& item) override;
    int firstEmpty(const item::ItemStack& item) override;
    void remove(const item::ItemStack& item) override;

    bool clear(int index) override;
    void clearAll() override;

    void onSlotChange(int index, const item::ItemStack& before) override;

    std::shared_ptr<InventoryType> getType() override {
        return type_;
    }
    InventoryHolder* getHolder() {
        return holder_;
    }

  protected:
    InventoryType* type_raw_ = nullptr; // cached for fast access
    std::shared_ptr<InventoryType> type_;
    int maxStackSize_ = MAX_STACK;
    int size_ = 0;
    std::string name_;
    std::string title_;
    std::unordered_map<int, item::ItemStack> slots_;
    InventoryHolder* holder_ = nullptr;
};

} // namespace cursestone::inventory
