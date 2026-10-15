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
// AddItemEntityPacket.h — server→client spawn of a dropped item entity.
//
// PMMP wire format (protocol 84) — AddItemEntityPacket::encode:
//   [1 byte ID 0x0d]
//   [Int64 BE] eid        runtime entity ID
//   [Slot]     item       PMMP Slot: id BE + count + damage BE + nbtLen LE + nbt
//   [Float BE] x
//   [Float BE] y
//   [Float BE] z
//   [Float BE] speedX
//   [Float BE] speedY
//   [Float BE] speedZ
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class AddItemEntityPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return ADD_ITEM_ENTITY_PACKET;
    }

    int64_t eid = 0;
    int16_t itemId = 0;
    uint8_t itemCount = 1;
    int16_t itemDamage = 0;
    // Raw little-endian NBT compound of the dropped stack (custom item
    // names survive being dropped thanks to this).
    std::string itemNbt;
    float x = 0, y = 0, z = 0;
    float speedX = 0, speedY = 0, speedZ = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
