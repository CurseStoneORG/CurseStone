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
// PlayerActionPacket.cpp — encode/decode.
//
// PMMP: putLong(eid) + putInt(action) + putInt(x) + putInt(y) + putInt(z) + putInt(face)
// All multi-byte fields are big-endian.
#include "PlayerActionPacket.h"

namespace cursestone::network::protocol {

void PlayerActionPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeI32BE(action);
    w.writeI32BE(x);
    w.writeI32BE(y);
    w.writeI32BE(z);
    w.writeI32BE(face);
}

bool PlayerActionPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid))
        return false;
    if (!r.readI32BE(action))
        return false;
    if (!r.readI32BE(x))
        return false;
    if (!r.readI32BE(y))
        return false;
    if (!r.readI32BE(z))
        return false;
    return r.readI32BE(face);
}

} // namespace cursestone::network::protocol
