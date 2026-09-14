// SPDX-License-Identifier: LGPL-3.0-or-later
// ExplodePacket.cpp — encoder per Infinity ExplodePacket.php.
#include "ExplodePacket.h"

namespace atomikpe::network::protocol {

void ExplodePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(radius);
    w.writeI32BE(0);   // records count — Infinity spawns Lightning with []
}

bool ExplodePacket::decode(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return true;  // server→client only
}

} // namespace atomikpe::network::protocol
