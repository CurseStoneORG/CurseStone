// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayStatusPacket.cpp — encode/decode.
//
// PMMP: $this->putInt($this->status)  (Int32 BE)
#include "PlayStatusPacket.h"

namespace atomikpe::network::protocol {

void PlayStatusPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(status);
}

bool PlayStatusPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI32BE(status);
}

} // namespace atomikpe::network::protocol
