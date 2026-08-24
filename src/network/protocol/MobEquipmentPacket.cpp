// SPDX-License-Identifier: LGPL-3.0-or-later
// MobEquipmentPacket.cpp — decode (client→server).
//
// PMMP decode: getInt(eid), getInt (unknown1), getSlot, getByte (slot), getByte (selectedSlot)
#include "MobEquipmentPacket.h"

namespace atomikpe::network::protocol {

namespace {
bool readSlot(PacketReader& r, int16_t& id, uint8_t& count, int16_t& damage) {
    if (!r.readI16BE(id)) return false;
    count = 0; damage = 0;
    if (id <= 0) { id = 0; return true; }
    if (!r.readU8(count))    return false;
    if (!r.readI16BE(damage)) return false;
    uint16_t nbtLen;
    if (!r.readU16LE(nbtLen)) return false;
    if (nbtLen > 0) {
        if (!r.skipBytes(nbtLen)) return false;
    }
    return true;
}
} // namespace

void MobEquipmentPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
}

bool MobEquipmentPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid))       return false;
    if (!r.readI32BE(unknown1))  return false;
    if (!readSlot(r, itemId, itemCount, itemDamage)) return false;
    if (!r.readU8(hotbarSlot)) return false;
    // PMMP reads a second byte (selectedSlot) if present.
    if (r.remaining() >= 1) {
        uint8_t sel; r.readU8(sel);
    }
    return true;
}

} // namespace atomikpe::network::protocol
