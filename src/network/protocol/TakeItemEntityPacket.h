// SPDX-License-Identifier: LGPL-3.0-or-later
// TakeItemEntityPacket — server→client pickup animation. Tells the
// client to play "entity target flies into entity entityId" animation.
// Used to visualize a player picking up a dropped item.
//
// PMMP wire format (protocol 84) — TakeItemEntityPacket::encode:
//   [1 byte ID 0x11]
//   [Int64 BE] target     entity being picked up (the dropped item)
//   [Int64 BE] entityId   entity picking up (the player)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class TakeItemEntityPacket : public DataPacket {
public:
    uint8_t pid() const override { return TAKE_ITEM_ENTITY_PACKET; }

    int64_t target    = 0;   // entity ID of the dropped item
    int64_t entityId  = 0;   // entity ID of the player picking up

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
