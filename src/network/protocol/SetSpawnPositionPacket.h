// SPDX-License-Identifier: LGPL-3.0-or-later
// SetSpawnPositionPacket.h — sync the player's spawn point.
//
// Wire format (protocol 84):
//   [1 byte ID 0x26]
//   [Int32 BE] x
//   [Int32 BE] y
//   [Int32 BE] z
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class SetSpawnPositionPacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_SPAWN_POSITION_PACKET; }

    int32_t x = 0;
    int32_t y = 64;
    int32_t z = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
