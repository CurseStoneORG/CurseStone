// SPDX-License-Identifier: LGPL-3.0-or-later
// TakeItemEntityPacket implementation.
#include "TakeItemEntityPacket.h"

namespace cursestone::network::protocol {

void TakeItemEntityPacket::encode(std::vector<uint8_t>& out) const {
    // Format: [1 byte ID 0x11] [Int64 BE target] [Int64 BE entityId]
    uint8_t buf[1 + 8 + 8];
    buf[0] = TAKE_ITEM_ENTITY_PACKET;

    int64_t target_be   = target;
    int64_t entityId_be = entityId;
    // Big-endian int64 expansion.
    for (int i = 0; i < 8; ++i) {
        buf[1 + i]     = (uint8_t)((target_be   >> (56 - i * 8)) & 0xFF);
        buf[9 + i]     = (uint8_t)((entityId_be >> (56 - i * 8)) & 0xFF);
    }
    out.assign(buf, buf + sizeof(buf));
}

bool TakeItemEntityPacket::decode(const uint8_t* /*data*/, size_t /*len*/) {
    // Server→client only; no decode path needed.
    return true;
}

} // namespace cursestone::network::protocol
