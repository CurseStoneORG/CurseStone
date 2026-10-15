/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
#include "ChangeDimensionPacket.h"

namespace cursestone::network::protocol {

// Wire format must match what the MCPE 0.15.10 client expects (protocol
// 84, same encoding PMMP/Genisys used): one BYTE for the dimension, three
// big-endian floats and a trailing zero byte. Encoding the dimension as
// an Int32 shifts every later field and the client silently drops the
// packet, which keeps the overworld sky/fog over nether terrain.
void ChangeDimensionPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8((uint8_t)dimension);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeU8(unknown);
}

bool ChangeDimensionPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    uint8_t dim = 0;
    if (!r.readU8(dim))
        return false;
    dimension = dim;
    if (!r.readF32BE(x))
        return false;
    if (!r.readF32BE(y))
        return false;
    if (!r.readF32BE(z))
        return false;
    return r.readU8(unknown);
}

} // namespace cursestone::network::protocol
