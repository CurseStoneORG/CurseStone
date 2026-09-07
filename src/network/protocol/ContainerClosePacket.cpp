// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerClosePacket implementation.
#include "ContainerClosePacket.h"

namespace atomikpe::network::protocol {

void ContainerClosePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
}

bool ContainerClosePacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readU8(windowid);
}

} // namespace atomikpe::network::protocol
