// SPDX-License-Identifier: LGPL-3.0-or-later
// MobEquipmentPacket.cpp — encode/decode.
//
// PMMP decode:
//   getInt (eid), getInt (unknown1), getSlot, getByte (slot), getByte (selectedSlot)
#include "MobEquipmentPacket.h"

namespace cenisyspe::network::protocol {

void MobEquipmentPacket::encode(std::vector<uint8_t>& out) const {
    // Serverbound only — nothing to encode.
    (void)out;
}

bool MobEquipmentPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid))       return false;
    if (!r.readI32BE(unknown1))  return false;

    // PMMP Slot.
    if (!r.readI16BE(itemId)) return false;
    if (itemId != 0) {
        if (!r.readU8(itemCount))   return false;
        if (!r.readI16BE(itemDamage)) return false;
    }

    if (!r.readU8(hotbarSlot)) return false;
    // PMMP reads a second byte (selectedSlot) — same value as hotbarSlot
    // in practice. We accept it if present but don't require it.
    uint8_t selectedSlot;
    if (r.remaining() >= 1) {
        r.readU8(selectedSlot);
    }
    return true;
}

} // namespace cenisyspe::network::protocol
