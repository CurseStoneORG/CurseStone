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
// ContainerSetSlotPacket — client→server item move in an inventory.
//
// PMMP wire format (protocol 84) — ContainerSetSlotPacket::decode:
//   [1 byte ID 0x2c]
//   [Uint8]    windowid     0=main, 0x78=armor, 0x79=creative, 0x7a=hotbar
//   [Int16 BE] slot         slot index in the inventory
//   [Int16 BE] hotbarSlot   destination hotbar slot (creative only)
//   [Slot]     item         PMMP Slot: id BE + count + damage BE + nbtLen LE + nbt
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class ContainerSetSlotPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return CONTAINER_SET_SLOT_PACKET;
    }

    static constexpr uint8_t SPECIAL_INVENTORY = 0x00;
    static constexpr uint8_t SPECIAL_ARMOR = 0x78;
    static constexpr uint8_t SPECIAL_CREATIVE = 0x79;
    static constexpr uint8_t SPECIAL_HOTBAR = 0x7a;

    uint8_t windowid = 0;
    int16_t slot = 0;
    int16_t hotbarSlot = 0;
    int16_t itemId = 0;
    uint8_t itemCount = 0;
    int16_t itemDamage = 0;
    // Raw little-endian NBT compound of the item (custom names live
    // here). Written after the slot's nbtLen short; empty = no NBT.
    std::string itemNbt;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
