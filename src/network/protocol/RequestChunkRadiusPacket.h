// SPDX-License-Identifier: LGPL-3.0-or-later
// RequestChunkRadiusPacket.h — client→server request for view-distance radius.
//
// Wire format (protocol 84):
//   [1 byte ID 0x3d]
//   [Int32 BE] radius  (PMMP: getInt)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class RequestChunkRadiusPacket : public DataPacket {
public:
    uint8_t pid() const override { return REQUEST_CHUNK_RADIUS_PACKET; }

    int32_t radius = 8;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
