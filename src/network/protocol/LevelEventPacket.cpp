// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEventPacket.cpp — encode (server→client) / decode (client→server, unused).
//
// PMMP encode: putInt(evid) + putFloat(x) + putFloat(y) + putFloat(z) + putInt(data)
#include "LevelEventPacket.h"

namespace cenisyspe::network::protocol {

void LevelEventPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(evid);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeI32BE(data);
}

bool LevelEventPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI32BE(evid)) return false;
    if (!r.readF32BE(x))    return false;
    if (!r.readF32BE(y))    return false;
    if (!r.readF32BE(z))    return false;
    if (!r.readI32BE(data)) return false;
    return true;
}

} // namespace cenisyspe::network::protocol
