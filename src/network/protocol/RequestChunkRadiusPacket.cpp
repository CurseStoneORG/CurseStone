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
// RequestChunkRadiusPacket.cpp — encode/decode.
// PMMP: getInt(radius) on decode.
#include "RequestChunkRadiusPacket.h"

namespace cursestone::network::protocol {

void RequestChunkRadiusPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(radius);
}

bool RequestChunkRadiusPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI32BE(radius);
}

} // namespace cursestone::network::protocol
