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
// UpdateBlockPacket.cpp — encode/decode.
//
// Genisys 0.15.0 UpdateBlockPacket::encode:
//     putInt(x); putInt(z); putByte(y); putByte(blockId);
//     putByte((flags << 4) | blockData);   // <-- flags+data, ONE byte
//
// All ints are big-endian (32-bit big-endian writes).
#include "UpdateBlockPacket.h"

namespace cursestone::network::protocol {

void UpdateBlockPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(x);
    w.writeI32BE(z);
    w.writeU8(y);
    w.writeU8(blockId);
    // Genisys: byte = (flags << 4) | blockData — flags in the HIGH nibble,
    // block data in the LOW nibble, both in one byte.
    w.writeU8((uint8_t)(((flags & 0x0F) << 4) | (blockData & 0x0F)));
}

bool UpdateBlockPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI32BE(x))
        return false;
    if (!r.readI32BE(z))
        return false;
    if (!r.readU8(y))
        return false;
    if (!r.readU8(blockId))
        return false;
    uint8_t combined = 0;
    if (!r.readU8(combined))
        return false;
    flags = (uint8_t)(combined >> 4);
    blockData = (uint8_t)(combined & 0x0F);
    return true;
}

} // namespace cursestone::network::protocol