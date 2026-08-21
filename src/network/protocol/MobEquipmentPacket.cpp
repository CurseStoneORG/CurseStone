// SPDX-License-Identifier: LGPL-3.0-or-later
// MobEquipmentPacket implementation (stub).
#include "MobEquipmentPacket.h"

namespace mcppeserver::network::protocol {

void MobEquipmentPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool MobEquipmentPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace mcppeserver::network::protocol
