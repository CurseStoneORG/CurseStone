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
// LevelEventPacket.cpp — encode (server→client) / decode (client→server, unused).
//
// The reference server encode (verbatim):
//   [Int16 BE] evid     // 2-byte BE signed short
//   [Float BE] x        // 4-byte BE float
//   [Float BE] y
//   [Float BE] z
//   [Int32 BE] data     // 4-byte BE signed int
//
// Total body size: 2 + 4 + 4 + 4 + 4 = 18 bytes.
#include "LevelEventPacket.h"

namespace cursestone::network::protocol {

void LevelEventPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI16BE(evid); // The reference server putShort -> 2-byte BE signed short
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeI32BE(data);
}

bool LevelEventPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    int16_t ev;
    if (!r.readI16BE(ev))
        return false;
    evid = ev;
    if (!r.readF32BE(x))
        return false;
    if (!r.readF32BE(y))
        return false;
    if (!r.readF32BE(z))
        return false;
    if (!r.readI32BE(data))
        return false;
    return true;
}

} // namespace cursestone::network::protocol
