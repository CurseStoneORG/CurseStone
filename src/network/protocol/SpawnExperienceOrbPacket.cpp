// SPDX-License-Identifier: LGPL-3.0-or-later
// SpawnExperienceOrbPacket implementation (stub).
#include "SpawnExperienceOrbPacket.h"

namespace atomikpe::network::protocol {

void SpawnExperienceOrbPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool SpawnExperienceOrbPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace atomikpe::network::protocol
