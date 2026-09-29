// SPDX-License-Identifier: LGPL-3.0-or-later
// MobArmorEquipmentPacket.cpp — decode (client→server).
#include "MobArmorEquipmentPacket.h"

namespace cursestone::network::protocol {

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

void MobArmorEquipmentPacket::encode(std::vector<uint8_t>& out) const {
    (void)out;
}

bool MobArmorEquipmentPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI64BE(eid)) return false;
    if (!readSlot(r, helmetId,      helmetCount,      helmetDamage))      return false;
    if (!readSlot(r, chestplateId,  chestplateCount,  chestplateDamage))  return false;
    if (!readSlot(r, leggingsId,    leggingsCount,    leggingsDamage))    return false;
    if (!readSlot(r, bootsId,       bootsCount,       bootsDamage))       return false;
    return true;
}

} // namespace cursestone::network::protocol
