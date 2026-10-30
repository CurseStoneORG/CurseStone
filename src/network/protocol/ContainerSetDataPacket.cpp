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
// ContainerSetDataPacket — implementation (encode server->client).
//
// Formato p84 (Dr1xyDev/Infinity + Nukkit 2016): shorts BIG-endian,
// NO varints:
//   putByte(windowid)
//   putShort(BE) property
//   putShort(BE) value
#include "ContainerSetDataPacket.h"

namespace cursestone::network::protocol {

void ContainerSetDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeI16BE(static_cast<int16_t>(property));
    w.writeI16BE(static_cast<int16_t>(value));
}

bool ContainerSetDataPacket::decode(const uint8_t* data, size_t len) {
    // Server->client only — like PMMP, empty decode.
    (void)data;
    (void)len;
    return true;
}

} // namespace cursestone::network::protocol
