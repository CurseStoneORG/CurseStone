// SPDX-License-Identifier: LGPL-3.0-or-later
// MapInfoRequestPacket implementation (stub).
#include "MapInfoRequestPacket.h"

namespace cursestone::network::protocol {

void MapInfoRequestPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool MapInfoRequestPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace cursestone::network::protocol
