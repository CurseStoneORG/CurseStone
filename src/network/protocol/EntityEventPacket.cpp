/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityEventPacket.cpp — encode/decode.
// The reference server: putLong(eid) + putByte(event) + the 32-bit data
// word the 0.15.10 client reads after the event byte. Clientbound only
// in CurseStone (server never needs to parse this coming FROM the
// client), but decode() is still implemented for symmetry/tests.
#include "EntityEventPacket.h"

namespace cursestone::network::protocol {

void EntityEventPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeU8(event);
    w.writeI32BE(data);
}

bool EntityEventPacket::decode(const uint8_t* data_, size_t len) {
    PacketReader r(data_, len);
    if (!r.readI64BE(eid))
        return false;
    if (!r.readU8(event))
        return false;
    return r.readI32BE(data);
}

} // namespace cursestone::network::protocol
