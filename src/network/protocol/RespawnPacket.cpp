// SPDX-License-Identifier: LGPL-3.0-or-later
// RespawnPacket.cpp — encode/decode.
// PMMP: putFloat(x) + putFloat(y) + putFloat(z). Clientbound only in
// practice, decode() implemented for symmetry/tests.
#include "RespawnPacket.h"

namespace cursestone::network::protocol {

void RespawnPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
}

bool RespawnPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readF32BE(x)) return false;
    if (!r.readF32BE(y)) return false;
    return r.readF32BE(z);
}

} // namespace cursestone::network::protocol
