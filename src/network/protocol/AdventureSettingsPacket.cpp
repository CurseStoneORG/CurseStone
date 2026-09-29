// SPDX-License-Identifier: LGPL-3.0-or-later
// AdventureSettingsPacket.cpp — encode/decode.
// PMMP: putInt x 3 (BE).
#include "AdventureSettingsPacket.h"

namespace cursestone::network::protocol {

void AdventureSettingsPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU32BE(flags);
    w.writeI32BE(userPermission);
    w.writeI32BE(globalPermission);
}

bool AdventureSettingsPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU32BE(flags))            return false;
    if (!r.readI32BE(userPermission))   return false;
    return r.readI32BE(globalPermission);
}

} // namespace cursestone::network::protocol
