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
// MobEquipmentPacket — client→server hotbar selection / item equip.
//
// MCPE protocol 84 (MCPE 0.15.10) wire format — Genisys
// MobEquipmentPacket::decode (iTXTech/Genisys, src/pocketmine/network/
// protocol/MobEquipmentPacket.php):
//
//   [1 byte  ID 0x1b]
//   [Long BE] eid           — getEntityId()
//   [Slot]    item          — getSlot()
//   [Byte]    slot          — getByte() — hotbar slot index 0..8
//   [Byte]    selectedSlot  — getByte() — selected slot on the client's
//                             hotbar UI (0..8)
//
// NOTE: Genisys (0.15.10, protocol 84) reads NO "unknown1" int32 here —
// that extra int32 only exists in the old pre-0.15 PocketMine packet,
// which is exactly why the previous decoder failed with
// "decode() fallo para ID 0x1b".
#pragma once

#include "DataPacket.h"
#include <string>
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class MobEquipmentPacket : public DataPacket {
public:
    uint8_t pid() const override { return MOB_EQUIPMENT_PACKET; }

    int64_t  eid = 0;
    // Decoded item slot (the item the player just selected/equipped).
    int16_t  itemId = 0;
    uint8_t  itemCount = 0;
    int16_t  itemDamage = 0;
    // Raw little-endian NBT compound the client sent with the slot
    // (custom item names); re-broadcast verbatim.
    std::string itemNbt;
    // Hotbar slot the player is now holding (0..8).
    uint8_t  hotbarSlot = 0;
    // Second byte sent by the client (selectedSlot in PMMP). Kept for
    // protocol fidelity; the server only acts on hotbarSlot.
    uint8_t  selectedSlot = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
