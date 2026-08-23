// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetSlotPacket — client→server item move in an inventory.
//
// PMMP wire format (protocol 84) — ContainerSetSlotPacket::decode:
//   [1 byte ID 0x2c]
//   [Uint8]    windowid     0=main, 0x78=armor, 0x79=creative, 0x7a=hotbar
//   [Int16 BE] slot         slot index in the inventory
//   [Int16 BE] hotbarSlot   destination hotbar slot (creative only)
//   [Slot]     item         PMMP Slot: id BE + count + damage BE + nbtLen LE + nbt
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::network::protocol {

class ContainerSetSlotPacket : public DataPacket {
public:
    uint8_t pid() const override { return CONTAINER_SET_SLOT_PACKET; }

    static constexpr uint8_t SPECIAL_INVENTORY = 0x00;
    static constexpr uint8_t SPECIAL_ARMOR     = 0x78;
    static constexpr uint8_t SPECIAL_CREATIVE  = 0x79;
    static constexpr uint8_t SPECIAL_HOTBAR    = 0x7a;

    uint8_t  windowid = 0;
    int16_t  slot = 0;
    int16_t  hotbarSlot = 0;
    int16_t  itemId = 0;
    uint8_t  itemCount = 0;
    int16_t  itemDamage = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
