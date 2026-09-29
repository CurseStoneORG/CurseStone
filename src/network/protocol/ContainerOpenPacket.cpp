// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerOpenPacket implementation. This is a server->client-only
// packet in practice (no legitimate client ever sends it), so decode()
// stays a permissive no-op like the rest of the outbound-only packets
// in this project — it just needs to not crash if a malicious/broken
// client ever sends bytes on this id.
#include "ContainerOpenPacket.h"

namespace cursestone::network::protocol {

void ContainerOpenPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeU8(type);
    w.writeI16BE(slots);
    w.writeI32BE(x);
    w.writeI32BE(y);
    w.writeI32BE(z);
    w.writeI64BE(entityId);
}

bool ContainerOpenPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return true;
}

} // namespace cursestone::network::protocol
