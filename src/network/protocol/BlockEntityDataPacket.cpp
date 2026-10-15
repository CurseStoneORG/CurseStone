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
// BlockEntityDataPacket implementation.
// Genisys: x/y/z = getInt() (BE big-endian); namedtag = get(true) (resto
// crudo del buffer).
#include "BlockEntityDataPacket.h"

namespace cursestone::network::protocol {

void BlockEntityDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(x);
    w.writeI32BE(y);
    w.writeI32BE(z);
    if (!namedtag.empty()) {
        w.writeBytes(namedtag.data(), namedtag.size());
    }
}

bool BlockEntityDataPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI32BE(x))
        return false;
    if (!r.readI32BE(y))
        return false;
    if (!r.readI32BE(z))
        return false;
    size_t rest = r.remaining();
    namedtag.assign(r.ptr(), r.ptr() + rest);
    return true;
}

} // namespace cursestone::network::protocol
