// SPDX-License-Identifier: LGPL-3.0-or-later
// RemoveBlockPacket.h — client→server block break confirmation.
//
// Wire format (protocol 84) — PMMP RemoveBlockPacket::decode:
//   [1 byte ID 0x12]
//   [Int64 BE] eid
//   [Int32 BE] x
//   [Int32 BE] z         (note: z, not y — see PMMP order)
//   [Uint8]    y
//
// Yes, MCPE protocol 84 quirks: x and z are Int32 BE, y is a single byte.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace mcppeserver::network::protocol {

class RemoveBlockPacket : public DataPacket {
public:
    uint8_t pid() const override { return REMOVE_BLOCK_PACKET; }

    int64_t eid = 0;
    int32_t x = 0, z = 0;
    uint8_t y = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace mcppeserver::network::protocol
