// SPDX-License-Identifier: LGPL-3.0-or-later
// ItemFrameDropItemPacket implementation.
// Genisys: z = getInt(); y = getInt(); x = getInt(); dropItem = getSlot().
#include "ItemFrameDropItemPacket.h"

namespace cursestone::network::protocol {

void ItemFrameDropItemPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
}

bool ItemFrameDropItemPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI32BE(z)) return false;
    if (!r.readI32BE(y)) return false;
    if (!r.readI32BE(x)) return false;
    if (!r.readI16BE(itemId)) return false;
    if (itemId <= 0) {
        itemId = 0;
        itemCount = 0;
        itemDamage = 0;
        return true;
    }
    if (!r.readU8(itemCount)) return false;
    if (!r.readI16BE(itemDamage)) return false;
    uint16_t nbtLen;
    if (!r.readU16LE(nbtLen)) return false;
    if (nbtLen > 0 && !r.skipBytes(nbtLen)) return false;
    return true;
}

} // namespace cursestone::network::protocol
