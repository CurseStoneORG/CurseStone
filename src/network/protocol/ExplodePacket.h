// SPDX-License-Identifier: LGPL-3.0-or-later
// ExplodePacket.h — client-side explosion/shockwave effect.
//
// Wire format (protocol 84, verified against Dr1xyDev/Infinity
// ExplodePacket.php):
//   [Float BE] x, y, z
//   [Float BE] radius
//   [Int   BE] records count (we send 0 records)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class ExplodePacket : public DataPacket {
public:
    uint8_t pid() const override { return EXPLODE_PACKET; }

    float x = 0, y = 0, z = 0;
    float radius = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
