// SPDX-License-Identifier: LGPL-3.0-or-later
// DisconnectPacket.cpp — encode/decode.
//
// PMMP: $this->putString($this->message)
// (putString = putShort BE length + bytes)
#include "DisconnectPacket.h"

namespace mcppeserver::network::protocol {

void DisconnectPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeBEString(message);
}

bool DisconnectPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readBEString(message);
}

} // namespace mcppeserver::network::protocol
