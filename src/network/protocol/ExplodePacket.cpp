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
// ExplodePacket.cpp — encoder per Infinity ExplodePacket.php.
#include "ExplodePacket.h"

namespace cursestone::network::protocol {

void ExplodePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(radius);
    w.writeI32BE(0); // records count — Infinity spawns Lightning with []
}

bool ExplodePacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return true; // server→client only
}

} // namespace cursestone::network::protocol
