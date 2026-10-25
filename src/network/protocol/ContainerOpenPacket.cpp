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
// ContainerOpenPacket implementation. This is a server->client-only
// packet in practice (no legitimate client ever sends it), so decode()
// stays a permissive no-op like the rest of the outbound-only packets
// in this project — it just needs to not crash if a malicious/broken
// client ever sends bytes on this id.
#include "ContainerOpenPacket.h"

namespace cursestone::network::protocol {

void ContainerOpenPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeU8(type);
    w.writeI16BE(slots);
    w.writeI32BE(x);
    w.writeI32BE(y);
    w.writeI32BE(z);
}

bool ContainerOpenPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return true;
}

} // namespace cursestone::network::protocol
