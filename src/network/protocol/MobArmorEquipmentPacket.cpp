// SPDX-License-Identifier: LGPL-3.0-or-later
// MobArmorEquipmentPacket.cpp — decode (client→server).
//
// PMMP decode: getInt(eid) + getSlot(helmet) + getSlot(chestplate)
//              + getSlot(leggings) + getSlot(boots)
//
// getSlot reads Int16 BE id; if id != 0: Uint8 count + Int16 BE damage.
#include "MobArmorEquipmentPacket.h"

namespace cenisyspe::network::protocol {

namespace {
// Helper — decode one PMMP slot.
bool decodeSlot(PacketReader& r, int16_t& id, uint8_t& count, int16_t& damage) {
    if (!r.readI16BE(id)) return false;
    if (id != 0) {
        if (!r.readU8(count))   return false;
        if (!r.readI16BE(damage)) return false;
    } else {
        count = 0;
        damage = 0;
    }
    return true;
}
} // namespace

void MobArmorEquipmentPacket::encode(std::vector<uint8_t>& out) const {
    // Serverbound only — nothing to encode (server doesn't broadcast this).
    (void)out;
}

bool MobArmorEquipmentPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readI64BE(eid)) return false;
    if (!decodeSlot(r, helmetId,      helmetCount,      helmetDamage))      return false;
    if (!decodeSlot(r, chestplateId,  chestplateCount,  chestplateDamage))  return false;
    if (!decodeSlot(r, leggingsId,    leggingsCount,    leggingsDamage))    return false;
    if (!decodeSlot(r, bootsId,       bootsCount,       bootsDamage))       return false;
    return true;
}

} // namespace cenisyspe::network::protocol
