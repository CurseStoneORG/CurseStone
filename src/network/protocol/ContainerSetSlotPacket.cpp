// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetSlotPacket.cpp — decode (client→server).
//
// PMMP decode:
//   getByte (windowid) + getShort (slot) + getShort (hotbarSlot) + getSlot (item)
#include "ContainerSetSlotPacket.h"

namespace cenisyspe::network::protocol {

void ContainerSetSlotPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeI16BE(slot);
    w.writeI16BE(hotbarSlot);
    w.writeI16BE(itemId);
    if (itemId != 0) {
        w.writeU8(itemCount);
        w.writeI16BE(itemDamage);
    }
}

bool ContainerSetSlotPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readU8(windowid)) return false;
    if (!r.readI16BE(slot)) return false;
    if (!r.readI16BE(hotbarSlot)) return false;
    if (!r.readI16BE(itemId)) return false;
    if (itemId != 0) {
        if (!r.readU8(itemCount))   return false;
        if (!r.readI16BE(itemDamage)) return false;
    }
    return true;
}

} // namespace cenisyspe::network::protocol
