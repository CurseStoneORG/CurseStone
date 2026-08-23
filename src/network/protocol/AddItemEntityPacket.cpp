// SPDX-License-Identifier: LGPL-3.0-or-later
// AddItemEntityPacket.cpp — encode (server→client).
//
// PMMP encode:
//   putLong(eid)  + putSlot(item) + 6 * putFloat
// All multi-byte values are big-endian (PMMP putLong/putFloat/putSlot).
#include "AddItemEntityPacket.h"

namespace cenisyspe::network::protocol {

void AddItemEntityPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    // Int64 BE — runtime entity ID
    w.writeI64BE(eid);
    // PMMP putSlot (protocol 84): Int16 BE id; if id != 0: count + damage
    w.writeI16BE(itemId);
    if (itemId != 0) {
        w.writeU8(itemCount);
        w.writeI16BE(itemDamage);
    }
    // Position + motion (6 floats, big-endian)
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(speedX);
    w.writeF32BE(speedY);
    w.writeF32BE(speedZ);
}

bool AddItemEntityPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI64BE(eid)) return false;
    if (!r.readI16BE(itemId)) return false;
    if (itemId != 0) {
        if (!r.readU8(itemCount))   return false;
        if (!r.readI16BE(itemDamage)) return false;
    }
    if (!r.readF32BE(x))      return false;
    if (!r.readF32BE(y))      return false;
    if (!r.readF32BE(z))      return false;
    if (!r.readF32BE(speedX)) return false;
    if (!r.readF32BE(speedY)) return false;
    if (!r.readF32BE(speedZ)) return false;
    return true;
}

} // namespace cenisyspe::network::protocol
