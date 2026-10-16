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
// UseItemPacket.h — client→server block placement / item use.
//
// The reference server wire format (protocol 84) — UseItemPacket::decode:
//   [1 byte ID 0x1f]
//   [Int32 BE] x         block being clicked
//   [Int32 BE] y
//   [Int32 BE] z
//   [Uint8]    face       (0=bottom, 1=top, 2=north, 3=south, 4=west, 5=east)
//   [Float BE] fx         cursor hit vector
//   [Float BE] fy
//   [Float BE] fz
//   [Float BE] posX       player position
//   [Float BE] posY
//   [Float BE] posZ
//   [Int32 BE] slot       inventory slot index of the used item
//   [Slot]     item       the reference server Slot (see below)
//
// The reference server Slot (BinaryStream::getSlot):
//   [Int16 BE] id       — if id <= 0, slot is EMPTY and the field ends here.
//   [Uint8]    count    — only present if id != 0.
//   [Int16 BE] damage   — only present if id != 0.
//   [Uint16 LE] nbtLen  — only present if id != 0. (LE, not BE!)
//   [nbtLen B] nbt      — only present if nbtLen > 0.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class UseItemPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return USE_ITEM_PACKET;
    }

    int32_t x = 0, y = 0, z = 0;
    uint8_t face = 0;
    float fx = 0, fy = 0, fz = 0;
    float posX = 0, posY = 0, posZ = 0;
    int32_t slot = 0;
    // Decoded item slot. id 0 = empty hand.
    int16_t itemId = 0;
    uint8_t itemCount = 0;
    int16_t itemDamage = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
