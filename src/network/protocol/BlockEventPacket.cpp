// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockEventPacket implementation (stub).
#include "BlockEventPacket.h"

namespace atomikpe::network::protocol {

void BlockEventPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool BlockEventPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace atomikpe::network::protocol
