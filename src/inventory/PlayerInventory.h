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
// PlayerInventory.h — ported from cn.nukkit.inventory.PlayerInventory
//
// Player inventory: 36 main slots + 4 armor slots + 9 hotbar reference
// slots. The hotbar links hotbar indices (0..8) to inventory slots.
//
// Layout (mirrors Nukkit):
//   slots 0..35  : main inventory (the bottom 36 of the 45-slot grid)
//   slots 36..39 : armor (helmet, chestplate, leggings, boots)
//   hotbar 0..8  : indices into the main inventory (default: 0..8)
//
// The "held slot" is the currently selected hotbar index (0..8).
#pragma once
#include "BaseInventory.h"
#include <array>
namespace cursestone::inventory {

// Backward-compat constants used by Player.cpp.
// SIZE is the number of slots EXCLUDING the 4 armor slots (Nukkit's
// getSize() returns super.getSize() - 4). Total storage = SIZE + 4.
static constexpr int PLAYER_INV_MAIN_SIZE = 36;
static constexpr int PLAYER_INV_ARMOR_COUNT = 4;
static constexpr int PLAYER_INV_TOTAL = PLAYER_INV_MAIN_SIZE + PLAYER_INV_ARMOR_COUNT; // 40
static constexpr int PLAYER_INV_HOTBAR_SIZE = 9;

class PlayerInventory : public BaseInventory {
  public:
    // Backward-compat constants.
    static constexpr int SIZE = PLAYER_INV_MAIN_SIZE;         // 36
    static constexpr int ARMOR_OFFSET = PLAYER_INV_MAIN_SIZE; // 36
    static constexpr int HELMET = ARMOR_OFFSET;               // 36
    static constexpr int CHESTPLATE = ARMOR_OFFSET + 1;       // 37
    static constexpr int LEGGINGS = ARMOR_OFFSET + 2;         // 38
    static constexpr int BOOTS = ARMOR_OFFSET + 3;            // 39

    // Default constructor — used by Player.cpp. Creates a Player-type
    // inventory with 40 slots (36 main + 4 armor) and 9 hotbar indices.
    PlayerInventory();

    // ---- Hotbar / held slot ----
    int getHotbarSize() const {
        return PLAYER_INV_HOTBAR_SIZE;
    }
    int getHotbarSlotIndex(int index) const;
    void setHotbarSlotIndex(int index, int slot);

    int getHeldItemIndex() const {
        return itemInHandIndex_;
    }
    void setHeldItemIndex(int index);

    int heldSlot() const {
        return getHotbarSlotIndex(itemInHandIndex_);
    }
    void setHeldSlot(int slot);

    item::ItemStack getHeldItem() {
        return getItem(heldSlot());
    }
    bool setItemInHand(const item::ItemStack& item) {
        return setItem(heldSlot(), item);
    }

    // ---- Armor accessors (mirrors Nukkit) ----
    item::ItemStack getHelmet() {
        return getItem(SIZE);
    }
    item::ItemStack getChestplate() {
        return getItem(SIZE + 1);
    }
    item::ItemStack getLeggings() {
        return getItem(SIZE + 2);
    }
    item::ItemStack getBoots() {
        return getItem(SIZE + 3);
    }

    bool setHelmet(const item::ItemStack& it) {
        return setItem(SIZE, it);
    }
    bool setChestplate(const item::ItemStack& it) {
        return setItem(SIZE + 1, it);
    }
    bool setLeggings(const item::ItemStack& it) {
        return setItem(SIZE + 2, it);
    }
    bool setBoots(const item::ItemStack& it) {
        return setItem(SIZE + 3, it);
    }

    item::ItemStack getArmorItem(int index) {
        return getItem(SIZE + index);
    }
    bool setArmorItem(int index, const item::ItemStack& it) {
        return setItem(SIZE + index, it);
    }

    std::array<item::ItemStack, 4> getArmorContents();
    void setArmorContents(const std::array<item::ItemStack, 4>& items);

    // ---- Nukkit overrides ----
    // getSize() returns the MAIN inventory size (36), NOT including armor.
    // The underlying storage has 40 slots (36 + 4 armor).
    int getSize() const override {
        return SIZE;
    }

    bool setItem(int index, const item::ItemStack& item) override;
    bool clear(int index) override;
    void clearAll() override;

    // sendSlot override: if the holder is the player themselves, use
    // windowid=0; otherwise use the player's windowId for this inventory.
    void sendSlot(int index, Player* player) override;
    void sendContents(Player* player) override;

    // Slot-change callback: called after setItem. Sends a confirmation
    // ContainerSetSlotPacket back to the holder so the client doesn't
    // revert the change (THIS WAS THE CREATIVE INVENTORY BUG FIX).
    void onSlotChange(int index, const item::ItemStack& before) override;

    // The player who owns this inventory. Set by Player on construction.
    // Used to know where to send slot-change packets.
    void setOwner(Player* p) {
        owner_ = p;
    }
    Player* getOwner() const {
        return owner_;
    }

  private:
    int itemInHandIndex_ = 0;
    std::array<int, PLAYER_INV_HOTBAR_SIZE> hotbar_;
    Player* owner_ = nullptr; // the Player who owns this inventory
};

} // namespace cursestone::inventory
