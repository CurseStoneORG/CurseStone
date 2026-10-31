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
// DoubleChestInventory — the vanilla large chest (InventoryLargeChest):
// two 27-slot chest halves addressed as one 54-slot container.
//
// Exactly like the vanilla reference, this wrapper owns NO storage of
// its own. Every slot read/write is forwarded to one of the two backing
// ChestInventory halves:
//   slots 0-26  → left half
//   slots 27-53 → right half
// Keeping the two TileChest halves as the single source of truth is what
// makes the rest vanilla too: each half persists its own 27 slots to its
// own tile NBT, and breaking one half drops only that half's slots while
// the other half keeps its items (no duplication, no re-indexing).
#pragma once
#include "BaseInventory.h"
#include "ChestInventory.h"
#include <memory>
namespace cursestone::inventory {

class DoubleChestInventory : public BaseInventory {
  public:
    DoubleChestInventory(std::shared_ptr<ChestInventory> left,
                         std::shared_ptr<ChestInventory> right);

    int getSize() const override {
        return size_;
    }

    const std::shared_ptr<ChestInventory>& leftSide() const {
        return left_;
    }
    const std::shared_ptr<ChestInventory>& rightSide() const {
        return right_;
    }

    // ---- Inventory interface: everything delegates to the halves ----
    item::ItemStack getItem(int index) override;
    bool setItem(int index, const item::ItemStack& item) override;

    std::vector<item::ItemStack> addItem(std::vector<item::ItemStack> slots) override;
    bool canAddItem(const item::ItemStack& item) override;
    std::vector<item::ItemStack> removeItem(std::vector<item::ItemStack> slots) override;

    std::unordered_map<int, item::ItemStack> getContents() override;
    void setContents(const std::unordered_map<int, item::ItemStack>& items) override;

    bool contains(const item::ItemStack& item) override;
    std::unordered_map<int, item::ItemStack> all(const item::ItemStack& item) override;
    int first(const item::ItemStack& item) override;
    int firstEmpty(const item::ItemStack& item) override;
    void remove(const item::ItemStack& item) override;

    bool clear(int index) override;
    void clearAll() override;

    // The container packet flow (Player::openChestAt / co-viewer sync)
    // sends its own ContainerSetContent/ContainerSetSlot packets with the
    // per-player window id — the base-class implementations would dump
    // the (always empty) BaseInventory slots_ map instead, so both sends
    // are deliberate no-ops here, same as every other tile container.
    void sendContents(Player* player) override;
    void sendSlot(int index, Player* player) override;

  private:
    static constexpr int HALF = 27;
    std::shared_ptr<ChestInventory> left_;
    std::shared_ptr<ChestInventory> right_;
};

} // namespace cursestone::inventory
