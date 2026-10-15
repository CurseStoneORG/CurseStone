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
// SetTimePacket.cpp — encode/decode.
// PMMP: putInt(time) + putByte(started ? 1 : 0).
#include "SetTimePacket.h"

namespace cursestone::network::protocol {

void SetTimePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(time);
    w.writeU8(started ? 1 : 0);
}

bool SetTimePacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    uint8_t s;
    if (!r.readI32BE(time))
        return false;
    if (!r.readU8(s))
        return false;
    started = (s != 0);
    return true;
}

} // namespace cursestone::network::protocol
