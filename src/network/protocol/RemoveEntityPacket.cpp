// SPDX-License-Identifier: LGPL-3.0-or-later
// RemoveEntityPacket.cpp — encode/decode.
// PMMP: putLong(eid).
#include "RemoveEntityPacket.h"

namespace mcppeserver::network::protocol {

void RemoveEntityPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
}

bool RemoveEntityPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI64BE(eid);
}

} // namespace mcppeserver::network::protocol
