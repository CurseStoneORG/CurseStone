// SPDX-License-Identifier: LGPL-3.0-or-later
// SetSpawnPositionPacket.cpp — encode/decode.
// PMMP: putInt x 3 (BE).
#include "SetSpawnPositionPacket.h"

namespace cursestone::network::protocol {

void SetSpawnPositionPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(x);
    w.writeI32BE(y);
    w.writeI32BE(z);
}

bool SetSpawnPositionPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI32BE(x)) return false;
    if (!r.readI32BE(y)) return false;
    return r.readI32BE(z);
}

} // namespace cursestone::network::protocol
