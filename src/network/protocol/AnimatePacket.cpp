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
// AnimatePacket.cpp — decode/encode (protocol 84).
//
// Genisys AnimatePacket (0.15.10) field order:
//   putVarInt(action) | putEntityId(eid)
//
// Actions (PMMP AnimatePacket): 1 = swing arm.
#include "AnimatePacket.h"

namespace cursestone::network::protocol {

void AnimatePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeVarU32(action);
    w.writeI64BE(eid);
}

bool AnimatePacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readVarU32(action))
        return false;
    if (!r.readI64BE(eid))
        return false;
    return true;
}

} // namespace cursestone::network::protocol
