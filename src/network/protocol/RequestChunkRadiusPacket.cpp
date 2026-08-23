// SPDX-License-Identifier: LGPL-3.0-or-later
// RequestChunkRadiusPacket.cpp — encode/decode.
// PMMP: getInt(radius) on decode.
#include "RequestChunkRadiusPacket.h"

namespace cenisyspe::network::protocol {

void RequestChunkRadiusPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(radius);
}

bool RequestChunkRadiusPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI32BE(radius);
}

} // namespace cenisyspe::network::protocol
