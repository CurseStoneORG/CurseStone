// SPDX-License-Identifier: LGPL-3.0-or-later
// RemoveEntityPacket.h — server→clients broadcast when an entity leaves view.
//
// Wire format (protocol 84):
//   [1 byte ID 0x0c]
//   [Int64 BE] eid
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace mcppeserver::network::protocol {

class RemoveEntityPacket : public DataPacket {
public:
    uint8_t pid() const override { return REMOVE_ENTITY_PACKET; }

    int64_t eid = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace mcppeserver::network::protocol
