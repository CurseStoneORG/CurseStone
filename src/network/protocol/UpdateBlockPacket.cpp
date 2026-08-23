// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateBlockPacket.cpp — encode/decode.
//
// PMMP: putInt(x) + putInt(z) + putByte(y) + putByte(blockId)
//       + putByte(blockData) + putByte(flags)
// All ints are big-endian.
#include "UpdateBlockPacket.h"

namespace cenisyspe::network::protocol {

void UpdateBlockPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(x);
    w.writeI32BE(z);
    w.writeU8(y);
    w.writeU8(blockId);
    w.writeU8(blockData);
    w.writeU8(flags);
}

bool UpdateBlockPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI32BE(x))         return false;
    if (!r.readI32BE(z))         return false;
    if (!r.readU8(y))            return false;
    if (!r.readU8(blockId))      return false;
    if (!r.readU8(blockData))    return false;
    return r.readU8(flags);
}

} // namespace cenisyspe::network::protocol
