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
// ContainerSetSlotPacket.cpp — decode (client→server).
//
// PMMP decode: getByte(windowid) + getShort(slot) + getShort(hotbarSlot) + getSlot(item)
#include "ContainerSetSlotPacket.h"

namespace cursestone::network::protocol {

void ContainerSetSlotPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeI16BE(slot);
    w.writeI16BE(hotbarSlot);
    if (itemId <= 0) {
        w.writeI16BE(0);   // empty slot
    } else {
        w.writeI16BE(itemId);
        w.writeU8(itemCount);
        w.writeI16BE(itemDamage);
        w.writeU16LE(0);    // nbtLen = 0
    }
}

bool ContainerSetSlotPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readU8(windowid)) return false;
    if (!r.readI16BE(slot)) return false;
    if (!r.readI16BE(hotbarSlot)) return false;
    if (!r.readI16BE(itemId)) return false;
    if (itemId <= 0) {
        itemId = 0;
        itemCount = 0;
        itemDamage = 0;
        return true;
    }
    if (!r.readU8(itemCount))   return false;
    if (!r.readI16BE(itemDamage)) return false;
    uint16_t nbtLen;
    if (!r.readU16LE(nbtLen)) return false;
    if (nbtLen > 0) {
        if (!r.skipBytes(nbtLen)) return false;
    }
    return true;
}

} // namespace cursestone::network::protocol
