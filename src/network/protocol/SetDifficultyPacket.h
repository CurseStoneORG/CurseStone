// SPDX-License-Identifier: LGPL-3.0-or-later
// SetDifficultyPacket.h — sync world difficulty to client.
//
// Wire format (protocol 84):
//   [1 byte ID 0x35]
//   [Int32 BE] difficulty (0=peaceful, 1=easy, 2=normal, 3=hard)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class SetDifficultyPacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_DIFFICULTY_PACKET; }

    int32_t difficulty = 1;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
