// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEventPacket.cpp — encode (server→client) / decode (client→server, unused).
//
// PMMP encode (verbatim):
//   $this->putShort($this->evid);   // 2-byte BE signed short
//   $this->putFloat($this->x);      // 4-byte BE float
//   $this->putFloat($this->y);
//   $this->putFloat($this->z);
//   $this->putInt($this->data);     // 4-byte BE signed int
//
// Total body size: 2 + 4 + 4 + 4 + 4 = 18 bytes.
#include "LevelEventPacket.h"

namespace cenisyspe::network::protocol {

void LevelEventPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    // PMMP putShort -> 2-byte big-endian signed short.
    w.writeI16BE(evid);
    // PMMP putFloat -> 4-byte big-endian float.
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    // PMMP putInt -> 4-byte big-endian signed int.
    w.writeI32BE(data);
}

bool LevelEventPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    int16_t ev;
    if (!r.readI16BE(ev)) return false;
    evid = ev;
    if (!r.readF32BE(x))    return false;
    if (!r.readF32BE(y))    return false;
    if (!r.readF32BE(z))    return false;
    if (!r.readI32BE(data)) return false;
    return true;
}

} // namespace cenisyspe::network::protocol
