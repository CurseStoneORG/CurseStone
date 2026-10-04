// SPDX-License-Identifier: LGPL-3.0-or-later
// SetEntityMotionPacket.cpp — encode/decode.
//
// Genisys 0.15.0: a flat run of {putLong(eid), putFloat(x), putFloat(y),
// putFloat(z)} records, with no count and no terminator.
#include "SetEntityMotionPacket.h"

namespace cursestone::network::protocol {

void SetEntityMotionPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    for (const Motion& m : entities) {
        w.writeI64BE(m.eid);
        w.writeF32BE(m.motX);
        w.writeF32BE(m.motY);
        w.writeF32BE(m.motZ);
    }
}

bool SetEntityMotionPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    entities.clear();
    while (r.remaining() >= 20) {
        Motion m;
        if (!r.readI64BE(m.eid)) return false;
        if (!r.readF32BE(m.motX)) return false;
        if (!r.readF32BE(m.motY)) return false;
        if (!r.readF32BE(m.motZ)) return false;
        entities.push_back(m);
    }
    return true;
}

} // namespace cursestone::network::protocol