// SPDX-License-Identifier: LGPL-3.0-or-later
// AnimatePacket.cpp — decode/encode (protocol 84).
//
// Genisys AnimatePacket (0.15.10) field order:
//   putVarInt(action) | putEntityId(eid)
//
// Actions (PMMP AnimatePacket): 1 = swing arm.
#include "AnimatePacket.h"

namespace cursestone::network::protocol {

void AnimatePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeVarU32(action);
    w.writeI64BE(eid);
}

bool AnimatePacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readVarU32(action)) return false;
    if (!r.readI64BE(eid))     return false;
    return true;
}

} // namespace cursestone::network::protocol
