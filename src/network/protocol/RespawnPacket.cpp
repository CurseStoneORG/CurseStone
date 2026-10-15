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
// RespawnPacket.cpp — encode/decode.
// PMMP: putFloat(x) + putFloat(y) + putFloat(z). Clientbound only in
// practice, decode() implemented for symmetry/tests.
#include "RespawnPacket.h"

namespace cursestone::network::protocol {

void RespawnPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
}

bool RespawnPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readF32BE(x))
        return false;
    if (!r.readF32BE(y))
        return false;
    return r.readF32BE(z);
}

} // namespace cursestone::network::protocol
