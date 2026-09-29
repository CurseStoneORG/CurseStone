// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetDataPacket — implementación (encode server→client).
#include "ContainerSetDataPacket.h"

namespace cursestone::network::protocol {

void ContainerSetDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeVarI32(property);
    w.writeVarI32(value);
}

bool ContainerSetDataPacket::decode(const uint8_t* data, size_t len) {
    // Server→client only — igual que PMMP, decode vacío.
    (void)data;
    (void)len;
    return true;
}

} // namespace cursestone::network::protocol
