// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerOpenPacket implementation (stub).
#include "ContainerOpenPacket.h"

namespace cenisyspe::network::protocol {

void ContainerOpenPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool ContainerOpenPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace cenisyspe::network::protocol
