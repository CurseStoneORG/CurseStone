// SPDX-License-Identifier: LGPL-3.0-or-later
// MovePlayerPacket.cpp — encode/decode.
//
// PMMP: putLong (BE) + 6× putFloat (BE) + putByte (mode) + putByte (onGround).
#include "MovePlayerPacket.h"

namespace cenisyspe::network::protocol {

void MovePlayerPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(yaw);
    w.writeF32BE(bodyYaw);
    w.writeF32BE(pitch);
    w.writeU8 (mode);
    w.writeU8 (onGround ? 1 : 0);
}

bool MovePlayerPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    uint8_t og;
    if (!r.readI64BE(eid))     return false;
    if (!r.readF32BE(x))       return false;
    if (!r.readF32BE(y))       return false;
    if (!r.readF32BE(z))       return false;
    if (!r.readF32BE(yaw))     return false;
    if (!r.readF32BE(bodyYaw)) return false;
    if (!r.readF32BE(pitch))   return false;
    if (!r.readU8 (mode))      return false;
    if (!r.readU8 (og))        return false;
    onGround = (og != 0);
    return true;
}

} // namespace cenisyspe::network::protocol
