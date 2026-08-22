// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateBlockPacket implementation (stub).
#include "UpdateBlockPacket.h"

namespace mcppeserver::network::protocol {

void UpdateBlockPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool UpdateBlockPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace mcppeserver::network::protocol
