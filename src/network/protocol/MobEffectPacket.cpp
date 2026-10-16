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
// MobEffectPacket implementation (protocol 84).
#include "MobEffectPacket.h"

namespace cursestone::network::protocol {

void MobEffectPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU64BE(eid);
    w.writeU8(eventId);
    w.writeU8(effectId);
    w.writeU8(amplifier);
    w.writeU8(particles ? 1 : 0);
    w.writeI32BE(duration);
}

bool MobEffectPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU64BE(eid))
        return false;
    if (!r.readU8(eventId))
        return false;
    if (!r.readU8(effectId))
        return false;
    if (!r.readU8(amplifier))
        return false;
    uint8_t particles_byte = 1;
    if (!r.readU8(particles_byte))
        return false;
    particles = particles_byte != 0;
    return r.readI32BE(duration);
}

} // namespace cursestone::network::protocol
