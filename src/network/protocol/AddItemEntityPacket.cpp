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
// AddItemEntityPacket.cpp — encode (server→client).
//
// PMMP encode:
//   putLong(eid)  + putSlot(item) + 6 * putFloat
// All multi-byte values are big-endian EXCEPT nbtLen which is LE.
#include "AddItemEntityPacket.h"

namespace cursestone::network::protocol {

void AddItemEntityPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    // PMMP putSlot
    if (itemId <= 0) {
        w.writeI16BE(0);   // empty slot
    } else {
        w.writeI16BE(itemId);
        w.writeU8(itemCount);
        w.writeI16BE(itemDamage);
        w.writeU16LE(0);    // nbtLen = 0 (no NBT)
    }
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(speedX);
    w.writeF32BE(speedY);
    w.writeF32BE(speedZ);
}

bool AddItemEntityPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI64BE(eid)) return false;
    if (!r.readI16BE(itemId)) return false;
    if (itemId <= 0) {
        itemId = 0;
    } else {
        if (!r.readU8(itemCount))   return false;
        if (!r.readI16BE(itemDamage)) return false;
        uint16_t nbtLen;
        if (!r.readU16LE(nbtLen)) return false;
        if (nbtLen > 0) {
            if (!r.skipBytes(nbtLen)) return false;
        }
    }
    if (!r.readF32BE(x))      return false;
    if (!r.readF32BE(y))      return false;
    if (!r.readF32BE(z))      return false;
    if (!r.readF32BE(speedX)) return false;
    if (!r.readF32BE(speedY)) return false;
    if (!r.readF32BE(speedZ)) return false;
    return true;
}

} // namespace cursestone::network::protocol
