// SPDX-License-Identifier: LGPL-3.0-or-later
// SetHealthPacket.cpp — encode/decode.
// PMMP: putInt(health).
#include "SetHealthPacket.h"

namespace atomikpe::network::protocol {

void SetHealthPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(health);
}

bool SetHealthPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI32BE(health);
}

} // namespace atomikpe::network::protocol
