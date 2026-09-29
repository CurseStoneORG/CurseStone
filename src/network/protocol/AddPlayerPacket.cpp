// SPDX-License-Identifier: LGPL-3.0-or-later
// AddPlayerPacket.cpp — encode/decode.
//
// PMMP putUUID writes 16 raw bytes from UUID::toBinary (LE for MCPE).
// PMMP putSlot when item id == 0 writes ONLY [Int16 BE 0] and returns.
//   See pocketmine Item::writeSlot / Binary::writeSlot in mcpe 0.15.x.
// PMMP AddPlayerPacket::encode writes yaw TWICE (the second putFloat is
//   tagged "//TODO headrot" in PMMP) — headYaw was never implemented in
//   protocol 84. Sending a different headYaw value misaligns the field
//   on the client side.
// PMMP Binary::writeMetadata for empty: [Uint8 0]  (count = 0).
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
    // headYaw was never implemented in protocol 84 — PMMP writes yaw twice
    // with a "//TODO headrot" comment. Mirror that exactly so the client
    // doesn't receive a desynced head rotation field.
    w.writeF32BE(yaw);            // headYaw = yaw (PMMP TODO headrot)
    w.writeF32BE(pitch);
    // Empty putSlot: PMMP emits ONLY putShort(0) when item id == 0
    // (2 bytes total). The previous code wrote two Int16 BE (4 bytes),
    // shifting every subsequent field by 2 bytes and corrupting metadata.
    w.writeI16BE(0);              // putSlot with id==0: only 2 bytes
    // Empty metadata: [Uint8 0].
    w.writeU8(0);
}

bool AddPlayerPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (r.remaining() < 16) return false;
    if (!r.readBytes(uuid.data(), 16)) return false;
    if (!r.readBEString(username)) return false;
    if (!r.readI64BE(eid))     return false;
    if (!r.readF32BE(x))       return false;
    if (!r.readF32BE(y))       return false;
    if (!r.readF32BE(z))       return false;
    if (!r.readF32BE(speedX))  return false;
    if (!r.readF32BE(speedY))  return false;
    if (!r.readF32BE(speedZ))  return false;
    if (!r.readF32BE(yaw))     return false;
    if (!r.readF32BE(headYaw)) return false;
    if (!r.readF32BE(pitch))   return false;
    // Empty slot is only 2 bytes (Int16 BE 0).
    int16_t slotId;
    if (!r.readI16BE(slotId))  return false;
    (void)slotId;
    uint8_t metaCount;
    return r.readU8(metaCount);
}

} // namespace cursestone::network::protocol
