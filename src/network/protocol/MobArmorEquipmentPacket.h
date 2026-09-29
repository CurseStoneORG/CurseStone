// SPDX-License-Identifier: LGPL-3.0-or-later
// MobArmorEquipmentPacket.h — client→server armor equip notification.
//
// PMMP wire format (protocol 84) — MobArmorEquipmentPacket::decode:
//   [1 byte ID 0x1c]
//   [Int64 BE] eid       entity ID (player's runtime ID)
//   [Slot]     helmet     PMMP Slot format
//   [Slot]     chestplate
//   [Slot]     leggings
//   [Slot]     boots
//
// PMMP ignores this packet server-side (the actual armor slot change is
// sent via ContainerSetSlotPacket with windowid=0x78); we register it
// just so PacketPool returns a non-null pointer and the network logger
// stops warning "unknown packet".
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class MobArmorEquipmentPacket : public DataPacket {
public:
    uint8_t pid() const override { return MOB_ARMOR_EQUIPMENT_PACKET; }

    int64_t  eid = 0;
    int16_t  helmetId = 0, chestplateId = 0, leggingsId = 0, bootsId = 0;
    uint8_t  helmetCount = 0, chestplateCount = 0, leggingsCount = 0, bootsCount = 0;
    int16_t  helmetDamage = 0, chestplateDamage = 0, leggingsDamage = 0, bootsDamage = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
