// SPDX-License-Identifier: LGPL-3.0-or-later
// MobEquipmentPacket.h — client→server hotbar selection / item equip.
//
// Wire format (protocol 84) — PMMP MobEquipmentPacket::decode:
//   [1 byte ID 0x1b]
//   [Int64 BE] eid
//   [Int32 BE] unknown1  (PMMP reads but ignores — "slot" range)
//   [Slot]     item       (variable-length)
//   [Uint8]    slot       (selected hotbar slot 0..8)
//   [Uint8]    selectedSlot (often same as slot)
//
// We only need `slot` (the hotbar slot the player is now holding) and the
// decoded item id so the server can keep its inventory state in sync.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::network::protocol {

class MobEquipmentPacket : public DataPacket {
public:
    uint8_t pid() const override { return MOB_EQUIPMENT_PACKET; }

    int64_t  eid = 0;
    int32_t  unknown1 = 0;
    // Decoded item slot.
    int16_t  itemId = 0;
    uint8_t  itemCount = 0;
    int16_t  itemDamage = 0;
    // Hotbar slot the player is now holding (0..8).
    uint8_t  hotbarSlot = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
