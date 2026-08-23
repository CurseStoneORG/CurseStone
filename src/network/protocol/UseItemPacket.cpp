// SPDX-License-Identifier: LGPL-3.0-or-later
// UseItemPacket.cpp — encode/decode.
//
// PMMP: putInt(x)+putInt(y)+putInt(z)+putByte(face)
//       +putFloat(fx)+putFloat(fy)+putFloat(fz)
//       +putFloat(posX)+putFloat(posY)+putFloat(posZ)
//       +putInt(slot)+Slot item
// All multi-byte fields are big-endian. We read up to `slot` and skip
// the trailing Slot (variable-length) for the MVP.
#include "UseItemPacket.h"

namespace mcppeserver::network::protocol {

void UseItemPacket::encode(std::vector<uint8_t>& out) const {
    // Serverbound only — nothing to encode.
    (void)out;
}

bool UseItemPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI32BE(x))   return false;
    if (!r.readI32BE(y))   return false;
    if (!r.readI32BE(z))   return false;
    if (!r.readU8(face))   return false;
    if (!r.readF32BE(fx))  return false;
    if (!r.readF32BE(fy))  return false;
    if (!r.readF32BE(fz))  return false;
    if (!r.readF32BE(posX))return false;
    if (!r.readF32BE(posY))return false;
    if (!r.readF32BE(posZ))return false;
    if (!r.readI32BE(slot))return false;
    // Skip the item slot (variable length) — we don't need it for
    // basic block placement.
    return true;
}

} // namespace mcppeserver::network::protocol
