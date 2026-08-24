// SPDX-License-Identifier: LGPL-3.0-or-later
// InteractPacket implementation (stub).
#include "InteractPacket.h"

namespace atomikpe::network::protocol {

void InteractPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool InteractPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace atomikpe::network::protocol
