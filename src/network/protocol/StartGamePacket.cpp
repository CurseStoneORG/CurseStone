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
// StartGamePacket.cpp — encode/decode.
//
// PMMP encode() uses putInt (BE), putByte, putLong (BE), putFloat (BE), putString (BE).
#include "StartGamePacket.h"

namespace cursestone::network::protocol {

void StartGamePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(seed);
    w.writeU8 (dimension);
    w.writeI32BE(generator);
    w.writeI32BE(gamemode);
    w.writeI64BE(eid);
    w.writeI32BE(spawnX);
    w.writeI32BE(spawnY);
    w.writeI32BE(spawnZ);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeU8(1);             // unknown flag, always 1
    w.writeU8(1);             // unknown flag, always 1
    w.writeU8(0);             // unknown, always 0
    w.writeBEString(unknown);
}

bool StartGamePacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI32BE(seed))       return false;
    if (!r.readU8 (dimension))    return false;
    if (!r.readI32BE(generator))  return false;
    if (!r.readI32BE(gamemode))   return false;
    if (!r.readI64BE(eid))        return false;
    if (!r.readI32BE(spawnX))     return false;
    if (!r.readI32BE(spawnY))     return false;
    if (!r.readI32BE(spawnZ))     return false;
    if (!r.readF32BE(x))          return false;
    if (!r.readF32BE(y))          return false;
    if (!r.readF32BE(z))          return false;
    uint8_t skip;
    if (!r.readU8(skip)) return false;
    if (!r.readU8(skip)) return false;
    if (!r.readU8(skip)) return false;
    return r.readBEString(unknown);
}

} // namespace cursestone::network::protocol
