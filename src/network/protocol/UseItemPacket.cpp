// SPDX-License-Identifier: LGPL-3.0-or-later
// UseItemPacket.cpp — encode/decode.
//
// PMMP: putInt(x)+putInt(y)+putInt(z)+putByte(face)
//       +putFloat(fx)+putFloat(fy)+putFloat(fz)
//       +putFloat(posX)+putFloat(posY)+putFloat(posZ)
//       +putInt(slot)+Slot item
// All multi-byte fields are big-endian EXCEPT the nbtLen inside the Slot
// which is little-endian (PMMP putLShort = pack("v", …)).
#include "UseItemPacket.h"

namespace cenisyspe::network::protocol {

namespace {
// PMMP Slot decoder (BinaryStream::getSlot).
//   id (Int16 BE); if id <= 0 → done. Otherwise:
//   count (Uint8), damage (Int16 BE), nbtLen (Uint16 LE), nbt (nbtLen bytes)
bool readSlot(PacketReader& r, int16_t& id, uint8_t& count, int16_t& damage) {
    if (!r.readI16BE(id)) return false;
    count = 0; damage = 0;
    if (id <= 0) { id = 0; return true; }
    if (!r.readU8(count))    return false;
    if (!r.readI16BE(damage)) return false;
    uint16_t nbtLen;
    if (!r.readU16LE(nbtLen)) return false;
    if (nbtLen > 0) {
        if (!r.skipBytes(nbtLen)) return false;
    }
    return true;
}
} // namespace

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
    return readSlot(r, itemId, itemCount, itemDamage);
}

} // namespace cenisyspe::network::protocol
