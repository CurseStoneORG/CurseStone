/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
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
// The NBT blob (custom item names) is captured into `nbt` instead of
// skipped, so a re-broadcast of the held item keeps the name.
bool readSlot(PacketReader& r, int16_t& id, uint8_t& count, int16_t& damage, std::string& nbt) {
    if (!r.readI16BE(id))
        return false;
    count = 0;
    damage = 0;
    nbt.clear();
    if (id <= 0) {
        id = 0;
        return true;
    } // empty/air slot: nothing follows
    if (!r.readU8(count))
        return false;
    if (!r.readI16BE(damage))
        return false;
    uint16_t nbtLen = 0;
    if (!r.readU16LE(nbtLen))
        return false;
    if (nbtLen > 0) {
        nbt.assign(nbtLen, '\0');
        if (!r.readBytes(reinterpret_cast<uint8_t*>(&nbt[0]), nbtLen)) {
            nbt.clear();
            return false;
        }
    }
    return true;
}

void writeSlot(PacketWriter& w, int16_t id, uint8_t count, int16_t damage, const std::string& nbt) {
    if (id <= 0) {
        w.writeI16BE(0); // air: just the zeroed id
        return;
    }
    w.writeI16BE(id);
    w.writeU8(count);
    w.writeI16BE(damage);
    w.writeU16LE((uint16_t)nbt.size()); // nbtLen (LE)
    if (!nbt.empty())
        w.writeBytes((const uint8_t*)nbt.data(), nbt.size());
}

} // namespace

void MobEquipmentPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    writeSlot(w, itemId, itemCount, itemDamage, itemNbt);
    w.writeU8(hotbarSlot);
    w.writeU8(selectedSlot);
}

bool MobEquipmentPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readI64BE(eid))
        return false;
    if (!readSlot(r, itemId, itemCount, itemDamage, itemNbt))
        return false;
    if (!r.readU8(hotbarSlot))
        return false;
    if (!r.readU8(selectedSlot))
        return false;
    return true;
}

} // namespace cursestone::network::protocol
