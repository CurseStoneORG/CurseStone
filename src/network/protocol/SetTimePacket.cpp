// SPDX-License-Identifier: LGPL-3.0-or-later
// SetTimePacket.cpp — encode/decode.
// PMMP: putInt(time) + putByte(started ? 1 : 0).
#include "SetTimePacket.h"

namespace cenisyspe::network::protocol {

void SetTimePacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(time);
    w.writeU8(started ? 1 : 0);
}

bool SetTimePacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    uint8_t s;
    if (!r.readI32BE(time)) return false;
    if (!r.readU8(s))       return false;
    started = (s != 0);
    return true;
}

} // namespace cenisyspe::network::protocol
