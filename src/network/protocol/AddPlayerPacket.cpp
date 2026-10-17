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
// The reference server Binary::writeMetadata always appends the 0x7f
//   terminator — even with no entries, so an empty blob is [Uint8 0x7f].
#include "AddPlayerPacket.h"
#include "SetEntityDataPacket.h"

#include <cstring>

namespace cursestone::network::protocol {

namespace {

// Same slot serialisation as MobEquipmentPacket: id/count/damage plus the
// raw little-endian NBT blob, with an empty slot shortened to the zeroed id.
void writeSlot(PacketWriter& w, int16_t id, uint8_t count, int16_t damage, const std::string& nbt) {
    if (id <= 0) {
        w.writeI16BE(0); // air: just the zeroed id
        return;
    }
    w.writeI16BE(id);
    w.writeU8(count);
    w.writeI16BE(damage);
    w.writeU16LE((uint16_t)nbt.size()); // nbtLen (LE)
    if (!nbt.empty())
        w.writeBytes((const uint8_t*)nbt.data(), nbt.size());
}

} // namespace

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
    // Held item: viewers draw the hand item straight from the spawn packet.
    writeSlot(w, itemId, itemCount, itemDamage, itemNbt);
    // Empty metadata: the reference server Binary::writeMetadata always
    // appends the 0x7f terminator (a plain 0 byte parses as a BYTE/FLAGS
    // entry and corrupts the stream tail).
    w.writeU8(SetEntityDataPacket::TERMINATOR);
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
    // Held slot (2 bytes when the id is 0; full slot otherwise).
    int16_t slotId;
    if (!r.readI16BE(slotId))
        return false;
    (void)slotId;
    // Metadata: single 0x7f terminator for the empty blob.
    uint8_t metaTerm;
    return r.readU8(metaTerm);
}

} // namespace cursestone::network::protocol
