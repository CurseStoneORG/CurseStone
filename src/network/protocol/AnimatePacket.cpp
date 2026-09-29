// SPDX-License-Identifier: LGPL-3.0-or-later
// AnimatePacket implementation (stub).
#include "AnimatePacket.h"

namespace cursestone::network::protocol {

void AnimatePacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
    // TODO: Phase 6 stub — implement when packets are wired into Player handlers.
}

bool AnimatePacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    // TODO: Phase 6 stub.
    return true;
}

} // namespace cursestone::network::protocol
