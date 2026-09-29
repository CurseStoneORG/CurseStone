// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetDataPacket — implementación (encode server→client).
//
// Formato p84 (Dr1xyDev/Infinity + Nukkit 2016): shorts BIG-endian,
// NO varints:
//   putByte(windowid)
//   putShort(BE) property
//   putShort(BE) value
#include "ContainerSetDataPacket.h"

namespace cursestone::network::protocol {

void ContainerSetDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeI16BE(static_cast<int16_t>(property));
    w.writeI16BE(static_cast<int16_t>(value));
}

bool ContainerSetDataPacket::decode(const uint8_t* data, size_t len) {
    // Server→client only — igual que PMMP, decode vacío.
    (void)data;
    (void)len;
    return true;
}

} // namespace cursestone::network::protocol
