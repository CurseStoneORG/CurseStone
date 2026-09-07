// SPDX-License-Identifier: LGPL-3.0-or-later
// InteractPacket.cpp — encode/decode.
// PMMP: getByte(action) + getLong(target). Serverbound only in
// practice (we never send this to a client), but encode() is
// implemented for symmetry/tests.
#include "InteractPacket.h"

namespace atomikpe::network::protocol {

void InteractPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(action);
    w.writeI64BE(target);
}

bool InteractPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU8(action)) return false;
    return r.readI64BE(target);
}

} // namespace atomikpe::network::protocol
