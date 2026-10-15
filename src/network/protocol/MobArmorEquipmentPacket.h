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
// MobArmorEquipmentPacket.h — client→server armor equip notification.
//
// PMMP wire format (protocol 84) — MobArmorEquipmentPacket::decode:
//   [1 byte ID 0x1c]
//   [Int64 BE] eid       entity ID (player's runtime ID)
//   [Slot]     helmet     PMMP Slot format
//   [Slot]     chestplate
//   [Slot]     leggings
//   [Slot]     boots
//
// PMMP ignores this packet server-side (the actual armor slot change is
// sent via ContainerSetSlotPacket with windowid=0x78); we register it
// just so PacketPool returns a non-null pointer and the network logger
// stops warning "unknown packet".
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class MobArmorEquipmentPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return MOB_ARMOR_EQUIPMENT_PACKET;
    }

    int64_t eid = 0;
    int16_t helmetId = 0, chestplateId = 0, leggingsId = 0, bootsId = 0;
    uint8_t helmetCount = 0, chestplateCount = 0, leggingsCount = 0, bootsCount = 0;
    int16_t helmetDamage = 0, chestplateDamage = 0, leggingsDamage = 0, bootsDamage = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
