// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityEventPacket.cpp — encode/decode.
// PMMP: putLong(eid) + putByte(event). Clientbound only in AtomikPE
// (server never needs to parse this coming FROM the client), but
// decode() is still implemented for symmetry/tests.
#include "EntityEventPacket.h"

namespace atomikpe::network::protocol {

void EntityEventPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeU8(event);
}

bool EntityEventPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid)) return false;
    return r.readU8(event);
}

} // namespace atomikpe::network::protocol
