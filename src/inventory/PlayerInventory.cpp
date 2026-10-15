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
// PlayerInventory.cpp — ported from cn.nukkit.inventory.PlayerInventory
#include "PlayerInventory.h"
#include "../Player.h"
#include "../network/protocol/ContainerSetContentPacket.h"
#include "../network/protocol/ContainerSetSlotPacket.h"
#include "../network/protocol/MobEquipmentPacket.h"
#include "../network/protocol/MobArmorEquipmentPacket.h"
#include "../utils/MainLogger.hpp"
#include <algorithm>
#include <cstring>

namespace cursestone::inventory {

// ---- Constructor ----
PlayerInventory::PlayerInventory()
    : BaseInventory(nullptr, InventoryType::get(InventoryType::PLAYER)) {
    // InventoryType PLAYER has defaultSize=49 (36+4+9). We use 40
    // (36 main + 4 armor) because hotbar is just a reference array,
    // not actual slots.
    size_ = PLAYER_INV_TOTAL; // 40
    hotbar_.fill(-1);
    // Default hotbar links: hotbar[0]→slot 0, hotbar[1]→slot 1, etc.
    for (int i = 0; i < PLAYER_INV_HOTBAR_SIZE; ++i) {
        hotbar_[i] = i;
    }
}

// ---- Hotbar ----
int PlayerInventory::getHotbarSlotIndex(int index) const {
    if (index >= 0 && index < PLAYER_INV_HOTBAR_SIZE)
        return hotbar_[index];
    return -1;
}

void PlayerInventory::setHotbarSlotIndex(int index, int slot) {
    if (index >= 0 && index < PLAYER_INV_HOTBAR_SIZE && slot >= -1 && slot < SIZE) {
        hotbar_[index] = slot;
    }
}

// ---- Held item index ----
void PlayerInventory::setHeldItemIndex(int index) {
    if (index >= 0 && index < PLAYER_INV_HOTBAR_SIZE) {
        itemInHandIndex_ = index;
        // Nukkit: sendHeldItem(viewers) — we skip the broadcast here
        // and let Player::onUpdate handle MobEquipmentPacket.
    }
}

void PlayerInventory::setHeldSlot(int slot) {
    if (slot >= -1 && slot < SIZE) {
        setHotbarSlotIndex(itemInHandIndex_, slot);
    }
}

// ---- setItem / clear / clearAll ----
bool PlayerInventory::setItem(int index, const item::ItemStack& item) {
    if (index < 0 || index >= size_)
        return false;
    if (item.id == 0 || item.count <= 0)
        return clear(index);

    item::ItemStack old = getItem(index);
    slots_[index] = item;
    onSlotChange(index, old);
    return true;
}

bool PlayerInventory::clear(int index) {
    if (slots_.count(index)) {
        item::ItemStack old = slots_[index];
        slots_.erase(index);
        onSlotChange(index, old);
    }
    return true;
}

void PlayerInventory::clearAll() {
    int limit = SIZE + 4;
    for (int i = 0; i < limit; ++i)
        clear(i);
}

// ---- Armor ----
std::array<item::ItemStack, 4> PlayerInventory::getArmorContents() {
    std::array<item::ItemStack, 4> armor;
    for (int i = 0; i < 4; ++i) {
        armor[i] = getItem(SIZE + i);
    }
    return armor;
}

void PlayerInventory::setArmorContents(const std::array<item::ItemStack, 4>& items) {
    for (int i = 0; i < 4; ++i) {
        if (items[i].id == 0) {
            clear(SIZE + i);
        } else {
            setItem(SIZE + i, items[i]);
        }
    }
}

// ---- onSlotChange (THE KEY FIX FOR CREATIVE INVENTORY) ----
void PlayerInventory::onSlotChange(int index, const item::ItemStack& before) {
    // CRITICAL: send a ContainerSetSlotPacket back to the owner so the
    // client confirms the slot change. Without this, MCPE 0.15.x reverts
    // the slot visually — the item disappears from the client UI even
    // though it's in the server's inventory. This was the root cause of
    // the "can't take items from creative" bug.
    if (owner_) {
        sendSlot(index, owner_);
    }
}

// ---- sendContents ----
// BUG FIX: same missing "9 dummy AIR slots" padding as
// Player::sendInventory() in Player.cpp — see the comment there for the
// full explanation (real PMMP's PlayerInventory::sendContents() pads 9
// extra AIR slots because the MCPE 0.15.x client under-renders windowid=0
// by exactly 9 slots otherwise). This override isn't currently called
// anywhere (Player::sendInventory() builds windowid=0 by hand instead),
// but it's part of the public Inventory interface, so keep it consistent
// with the real fix in case something starts calling it.
void PlayerInventory::sendContents(Player* player) {
    if (!player)
        return;
    auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
    pk->windowid = 0; // SPECIAL_INVENTORY
    pk->slots.reserve(SIZE + 9);
    for (int i = 0; i < SIZE; ++i) {
        pk->slots.push_back(getItem(i));
    }
    for (int i = 0; i < 9; ++i) {
        pk->slots.push_back(item::ItemStack::EMPTY());
    }
    // Hotbar link indices.
    for (int i = 0; i < PLAYER_INV_HOTBAR_SIZE; ++i) {
        int idx = getHotbarSlotIndex(i);
        pk->hotbar.push_back(idx <= -1 ? -1 : idx + 9);
    }
    player->sendPacket(pk);
}

// ---- sendSlot ----
void PlayerInventory::sendSlot(int index, Player* player) {
    if (!player)
        return;
    auto pk = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    pk->slot = static_cast<int16_t>(index);
    pk->hotbarSlot = static_cast<int16_t>(index);
    auto item = getItem(index);
    pk->itemId = static_cast<int16_t>(item.id);
    pk->itemCount = item.count;
    pk->itemDamage = static_cast<int16_t>(item.meta);
    // The confirmation slot MUST carry the stack's NBT blob. This packet
    // is what the client uses to re-render the slot after every change;
    // without the {display:{Name}} compound a custom-named stack was
    // re-drawn with its default name (the "sword without its custom
    // name" bug — the inventory data was correct, only this ack was
    // lossy). Genisys' ContainerSetSlotPacket encodes the full
    // putSlot() payload exactly like this.
    pk->itemNbt = item.nbt;

    // Determine windowid.
    if (player == owner_) {
        // The owner gets windowid=0 (main inventory) or 0x78 (armor).
        if (index >= SIZE && index < SIZE + 4) {
            pk->windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR;
            pk->slot = static_cast<int16_t>(index - SIZE);
        } else {
            pk->windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY;
        }
    } else {
        pk->windowid = 0; // fallback
    }
    player->sendPacket(pk);
}

} // namespace cursestone::inventory
