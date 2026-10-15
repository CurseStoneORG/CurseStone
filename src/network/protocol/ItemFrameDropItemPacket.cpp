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
// ItemFrameDropItemPacket implementation.
// Genisys: z = getInt(); y = getInt(); x = getInt(); dropItem = getSlot().
#include "ItemFrameDropItemPacket.h"

namespace cursestone::network::protocol {

void ItemFrameDropItemPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
}

bool ItemFrameDropItemPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI32BE(z))
        return false;
    if (!r.readI32BE(y))
        return false;
    if (!r.readI32BE(x))
        return false;
    if (!r.readI16BE(itemId))
        return false;
    if (itemId <= 0) {
        itemId = 0;
        itemCount = 0;
        itemDamage = 0;
        return true;
    }
    if (!r.readU8(itemCount))
        return false;
    if (!r.readI16BE(itemDamage))
        return false;
    uint16_t nbtLen;
    if (!r.readU16LE(nbtLen))
        return false;
    if (nbtLen > 0 && !r.skipBytes(nbtLen))
        return false;
    return true;
}

} // namespace cursestone::network::protocol
