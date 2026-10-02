// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockEntityDataPacket implementation.
// Genisys: x/y/z = getInt() (BE big-endian); namedtag = get(true) (resto
// crudo del buffer).
#include "BlockEntityDataPacket.h"

namespace cursestone::network::protocol {

void BlockEntityDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(x);
    w.writeI32BE(y);
    w.writeI32BE(z);
    if (!namedtag.empty()) {
        w.writeBytes(namedtag.data(), namedtag.size());
    }
}

bool BlockEntityDataPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI32BE(x)) return false;
    if (!r.readI32BE(y)) return false;
    if (!r.readI32BE(z)) return false;
    size_t rest = r.remaining();
    namedtag.assign(r.ptr(), r.ptr() + rest);
    return true;
}

} // namespace cursestone::network::protocol
