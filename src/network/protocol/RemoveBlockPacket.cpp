// SPDX-License-Identifier: LGPL-3.0-or-later
// RemoveBlockPacket.cpp — encode/decode.
//
// PMMP: putLong(eid) + putInt(x) + putInt(z) + putByte(y)
// All multi-byte fields are big-endian. Note that y is a single byte
// (0..255), which is fine because the world is 256 blocks tall.
#include "RemoveBlockPacket.h"

namespace mcppeserver::network::protocol {

void RemoveBlockPacket::encode(std::vector<uint8_t>& out) const {
    // Serverbound only — nothing to encode.
    (void)out;
}

bool RemoveBlockPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid)) return false;
    if (!r.readI32BE(x))   return false;
    if (!r.readI32BE(z))   return false;
    return r.readU8(y);
}

} // namespace mcppeserver::network::protocol
