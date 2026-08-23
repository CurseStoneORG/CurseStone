// SPDX-License-Identifier: LGPL-3.0-or-later
// UseItemPacket.cpp — encode/decode.
//
// PMMP: putInt(x)+putInt(y)+putInt(z)+putByte(face)
//       +putFloat(fx)+putFloat(fy)+putFloat(fz)
//       +putFloat(posX)+putFloat(posY)+putFloat(posZ)
//       +putInt(slot)+Slot item
// All multi-byte fields are big-endian.
#include "UseItemPacket.h"

namespace cenisyspe::network::protocol {

void UseItemPacket::encode(std::vector<uint8_t>& out) const {
    // Serverbound only — nothing to encode.
    (void)out;
}

bool UseItemPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI32BE(x))    return false;
    if (!r.readI32BE(y))    return false;
    if (!r.readI32BE(z))    return false;
    if (!r.readU8(face))    return false;
    if (!r.readF32BE(fx))   return false;
    if (!r.readF32BE(fy))   return false;
    if (!r.readF32BE(fz))   return false;
    if (!r.readF32BE(posX)) return false;
    if (!r.readF32BE(posY)) return false;
    if (!r.readF32BE(posZ)) return false;
    if (!r.readI32BE(slot)) return false;

    // PMMP Slot (Binary::readSlot):
    //   [Int16 BE] id     — if 0, slot is empty and field ends here.
    //   [Uint8]    count  — present only if id != 0.
    //   [Int16 BE] damage — present only if id != 0.
    if (!r.readI16BE(itemId)) return false;
    if (itemId != 0) {
        if (!r.readU8(itemCount))   return false;
        if (!r.readI16BE(itemDamage)) return false;
    }
    return true;
}

} // namespace cenisyspe::network::protocol
