// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateBlockPacket.cpp — encode/decode.
//
// Genisys 0.15.0 UpdateBlockPacket::encode:
//     putInt(x); putInt(z); putByte(y); putByte(blockId);
//     putByte((flags << 4) | blockData);   // <-- flags+data, ONE byte
//
// All ints are big-endian (PMMP Binary::writeInt -> pack("N", ...)).
#include "UpdateBlockPacket.h"

namespace cursestone::network::protocol {

void UpdateBlockPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(x);
    w.writeI32BE(z);
    w.writeU8(y);
    w.writeU8(blockId);
    // Genisys: putByte(($r[5] << 4) | $r[4]) — flags in the HIGH nibble,
    // block data in the LOW nibble, both in one byte.
    w.writeU8((uint8_t)(((flags & 0x0F) << 4) | (blockData & 0x0F)));
}

bool UpdateBlockPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI32BE(x))      return false;
    if (!r.readI32BE(z))      return false;
    if (!r.readU8(y))         return false;
    if (!r.readU8(blockId))   return false;
    uint8_t combined = 0;
    if (!r.readU8(combined))  return false;
    flags     = (uint8_t)(combined >> 4);
    blockData = (uint8_t)(combined & 0x0F);
    return true;
}

} // namespace cursestone::network::protocol