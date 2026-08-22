// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkRadiusUpdatedPacket.h — server tells client the agreed view-distance.
//
// Wire format (protocol 84):
//   [1 byte ID 0x3e]
//   [Int32 BE] radius   (PMMP putInt)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace mcppeserver::network::protocol {

class ChunkRadiusUpdatedPacket : public DataPacket {
public:
    uint8_t pid() const override { return CHUNK_RADIUS_UPDATED_PACKET; }

    int32_t radius = 8;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace mcppeserver::network::protocol
