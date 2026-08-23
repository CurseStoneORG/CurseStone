// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerActionPacket.cpp — encode/decode.
//
// PMMP: putLong(eid) + putInt(action) + putInt(x) + putInt(y) + putInt(z) + putInt(face)
// All multi-byte fields are big-endian.
#include "PlayerActionPacket.h"

namespace cenisyspe::network::protocol {

void PlayerActionPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeI32BE(action);
    w.writeI32BE(x);
    w.writeI32BE(y);
    w.writeI32BE(z);
    w.writeI32BE(face);
}

bool PlayerActionPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid))    return false;
    if (!r.readI32BE(action)) return false;
    if (!r.readI32BE(x))      return false;
    if (!r.readI32BE(y))      return false;
    if (!r.readI32BE(z))      return false;
    return r.readI32BE(face);
}

} // namespace cenisyspe::network::protocol
