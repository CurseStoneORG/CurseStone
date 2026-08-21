// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkRadiusUpdatedPacket.cpp — encode/decode.
// PMMP: putInt(radius).
#include "ChunkRadiusUpdatedPacket.h"

namespace mcppeserver::network::protocol {

void ChunkRadiusUpdatedPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(radius);
}

bool ChunkRadiusUpdatedPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI32BE(radius);
}

} // namespace mcppeserver::network::protocol
