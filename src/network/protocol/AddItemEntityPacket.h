// SPDX-License-Identifier: LGPL-3.0-or-later
// AddItemEntityPacket.h — server→client spawn of a dropped item entity.
//
// PMMP wire format (protocol 84) — AddItemEntityPacket::encode:
//   [1 byte ID 0x0d]
//   [Int64 BE] eid        runtime entity ID
//   [Slot]     item       PMMP Slot: id BE + count + damage BE + nbtLen LE + nbt
//   [Float BE] x
//   [Float BE] y
//   [Float BE] z
//   [Float BE] speedX
//   [Float BE] speedY
//   [Float BE] speedZ
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::network::protocol {

class AddItemEntityPacket : public DataPacket {
public:
    uint8_t pid() const override { return ADD_ITEM_ENTITY_PACKET; }

    int64_t  eid = 0;
    int16_t  itemId = 0;
    uint8_t  itemCount = 1;
    int16_t  itemDamage = 0;
    float    x = 0, y = 0, z = 0;
    float    speedX = 0, speedY = 0, speedZ = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
