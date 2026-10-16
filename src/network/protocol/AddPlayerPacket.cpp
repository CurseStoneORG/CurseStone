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
// AddPlayerPacket.cpp — encode/decode.
//
// The reference server putUUID writes 16 raw bytes from UUID::toBinary (LE for MCPE).
// The reference server putSlot when item id == 0 writes ONLY [Int16 BE 0] and returns.
//   See the reference server Item::writeSlot / Binary::writeSlot in mcpe 0.15.x.
// The reference server AddPlayerPacket::encode writes yaw TWICE (the second putFloat is
//   tagged "//TODO headrot" in the reference server) — headYaw was never implemented in
//   protocol 84. Sending a different headYaw value misaligns the field
//   on the client side.
// The reference server Binary::writeMetadata for empty: [Uint8 0]  (count = 0).
#include "AddPlayerPacket.h"

#include <cstring>

namespace cursestone::network::protocol {

void AddPlayerPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeBytes(uuid.data(), 16);
    w.writeBEString(username);
    w.writeI64BE(eid);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(speedX);
    w.writeF32BE(speedY);
    w.writeF32BE(speedZ);
    w.writeF32BE(yaw);
    // headYaw was never implemented in protocol 84 — the reference server writes yaw twice
    // with a "//TODO headrot" comment. Mirror that exactly so the client
    // doesn't receive a desynced head rotation field.
    w.writeF32BE(yaw); // headYaw = yaw (the reference server TODO headrot)
    w.writeF32BE(pitch);
    // Empty putSlot: The reference server emits ONLY putShort(0) when item id == 0
    // (2 bytes total). The previous code wrote two Int16 BE (4 bytes),
    // shifting every subsequent field by 2 bytes and corrupting metadata.
    w.writeI16BE(0); // putSlot with id==0: only 2 bytes
    // Empty metadata: [Uint8 0].
    w.writeU8(0);
}

bool AddPlayerPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (r.remaining() < 16)
        return false;
    if (!r.readBytes(uuid.data(), 16))
        return false;
    if (!r.readBEString(username))
        return false;
    if (!r.readI64BE(eid))
        return false;
    if (!r.readF32BE(x))
        return false;
    if (!r.readF32BE(y))
        return false;
    if (!r.readF32BE(z))
        return false;
    if (!r.readF32BE(speedX))
        return false;
    if (!r.readF32BE(speedY))
        return false;
    if (!r.readF32BE(speedZ))
        return false;
    if (!r.readF32BE(yaw))
        return false;
    if (!r.readF32BE(headYaw))
        return false;
    if (!r.readF32BE(pitch))
        return false;
    // Empty slot is only 2 bytes (Int16 BE 0).
    int16_t slotId;
    if (!r.readI16BE(slotId))
        return false;
    (void)slotId;
    uint8_t metaCount;
    return r.readU8(metaCount);
}

} // namespace cursestone::network::protocol
