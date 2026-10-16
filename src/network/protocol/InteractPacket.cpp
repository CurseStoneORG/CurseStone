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
// InteractPacket.cpp — encode/decode.
// The reference server: getByte(action) + getLong(target). Serverbound only in
// practice (we never send this to a client), but encode() is
// implemented for symmetry/tests.
#include "InteractPacket.h"

namespace cursestone::network::protocol {

void InteractPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(action);
    w.writeI64BE(target);
}

bool InteractPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU8(action))
        return false;
    return r.readI64BE(target);
}

} // namespace cursestone::network::protocol
