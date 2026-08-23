// SPDX-License-Identifier: LGPL-3.0-or-later
// SetPlayerGameTypePacket implementation.
#include "SetPlayerGameTypePacket.h"

namespace cenisyspe::network::protocol {

void SetPlayerGameTypePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(gamemode);
}

bool SetPlayerGameTypePacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI32BE(gamemode);
}

} // namespace cenisyspe::network::protocol
