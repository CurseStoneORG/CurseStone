// SPDX-License-Identifier: LGPL-3.0-or-later
// AddEntityPacket.cpp — encoder per Infinity AddEntityPacket.php.
#include "AddEntityPacket.h"

namespace cursestone::network::protocol {

void AddEntityPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeI32BE(type);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(speedX);
    w.writeF32BE(speedY);
    w.writeF32BE(speedZ);
    // Infinity: $this->putFloat($this->yaw * 0.71111) — the 0.15.x
    // client expects the packed 0..255 rotation representation.
    constexpr float kRotScale = 0.71111f;
    w.writeF32BE(yawPitchScaled ? yaw * kRotScale : yaw);
    w.writeF32BE(yawPitchScaled ? pitch * kRotScale : pitch);
    w.writeI32BE(modifiers);
    // Empty metadata: single 0 byte (same convention as AddPlayerPacket).
    w.writeU8(0);
    // No entity links.
    w.writeI16BE(0);
}

bool AddEntityPacket::decode(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return true;  // server→client only
}

} // namespace cursestone::network::protocol
