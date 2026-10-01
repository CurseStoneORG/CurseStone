// SPDX-License-Identifier: LGPL-3.0-or-later
// MobEquipmentPacket.cpp — decode/encode (protocol 84).
//
// Genisys MobEquipmentPacket (0.15.10) field order:
//   putEntityId(eid) | putSlot(item) | putByte(slot) | putByte(selectedSlot)
//
// putEntityId() writes a big-endian int64; putSlot() writes
// short(id) + byte(count) + short(damage) + short(nbtLen) [+ nbt].
#include "MobEquipmentPacket.h"

namespace cursestone::network::protocol {

namespace {

// Shared slot reader — same encoding as ContainerSetSlotPacket::getSlot().
bool readSlot(PacketReader& r, int16_t& id, uint8_t& count, int16_t& damage) {
    if (!r.readI16BE(id)) return false;
    count = 0;
    damage = 0;
    if (id <= 0) { id = 0; return true; }   // empty/air slot: nothing follows
    if (!r.readU8(count))    return false;
    if (!r.readI16BE(damage)) return false;
    uint16_t nbtLen = 0;
    if (!r.readU16LE(nbtLen)) return false;
    if (nbtLen > 0 && !r.skipBytes(nbtLen)) return false;
    return true;
}

void writeSlot(PacketWriter& w, int16_t id, uint8_t count, int16_t damage) {
    if (id <= 0) {
        w.writeI16BE(0);                    // air: just the zeroed id
        return;
    }
    w.writeI16BE(id);
    w.writeU8(count);
    w.writeI16BE(damage);
    w.writeU16LE(0);                        // nbtLen = 0
}

} // namespace

void MobEquipmentPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    writeSlot(w, itemId, itemCount, itemDamage);
    w.writeU8(hotbarSlot);
    w.writeU8(selectedSlot);
}

bool MobEquipmentPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid))  return false;
    if (!readSlot(r, itemId, itemCount, itemDamage)) return false;
    if (!r.readU8(hotbarSlot))   return false;
    if (!r.readU8(selectedSlot)) return false;
    return true;
}

} // namespace cursestone::network::protocol
