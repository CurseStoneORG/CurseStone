// SPDX-License-Identifier: LGPL-3.0-or-later
// ItemFrameDropItemPacket — cliente→servidor. El jugador rompe/golpea un
// item frame para soltar el item que cuelga de él.
//
// Wire format (protocolo 84, mirrors Genisys ItemFrameDropItemPacket.php):
//   [Int BE] z
//   [Int BE] y
//   [Int BE] x
//   [Slot]   dropItem      // item que el cliente tenía en el frame
// (el orden z,y,x es el que usa Genisys, no x,y,z)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class ItemFrameDropItemPacket : public DataPacket {
public:
    uint8_t pid() const override { return ITEM_FRAME_DROP_ITEM_PACKET; }

    int32_t x = 0, y = 0, z = 0;
    int16_t itemId     = 0;
    uint8_t itemCount  = 0;
    int16_t itemDamage = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
