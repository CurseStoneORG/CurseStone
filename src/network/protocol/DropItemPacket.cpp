// SPDX-License-Identifier: LGPL-3.0-or-later
// DropItemPacket implementation (cliente→servidor).
// Genisys: type = getByte(); item = getSlot(). El servidor no manda este
// paquete, así que encode() queda vacío (igual que en Genisys).
#include "DropItemPacket.h"

namespace cursestone::network::protocol {

void DropItemPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
}

bool DropItemPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readU8(type)) return false;
    if (!r.readI16BE(itemId)) return false;
    if (itemId <= 0) {          // slot vacío
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
