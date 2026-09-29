// SPDX-License-Identifier: LGPL-3.0-or-later
// ClientboundMapItemDataPacket implementation (stub).
#include "ClientboundMapItemDataPacket.h"

namespace cursestone::network::protocol {

void ClientboundMapItemDataPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool ClientboundMapItemDataPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace cursestone::network::protocol
