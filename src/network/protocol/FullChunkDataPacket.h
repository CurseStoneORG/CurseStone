// SPDX-License-Identifier: LGPL-3.0-or-later
// FullChunkDataPacket.h — send a fully-serialized chunk to the client.
//
// Wire format (protocol 84) — PMMP FullChunkDataPacket::encode:
//   [1 byte ID 0x34]
//   [Int32 BE] chunkX
//   [Int32 BE] chunkZ
//   [Uint8]   order   (0 = ORDER_COLUMNS, 1 = ORDER_LAYERED)
//   [Int32 BE] data length
//   [N bytes ] data   (chunk payload serialized by mcp_chunk_network_serialize)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace mcppeserver::network::protocol {

class FullChunkDataPacket : public DataPacket {
public:
    uint8_t pid() const override { return FULL_CHUNK_DATA_PACKET; }

    static constexpr uint8_t ORDER_COLUMNS = 0;
    static constexpr uint8_t ORDER_LAYERED = 1;

    int32_t  chunkX = 0;
    int32_t  chunkZ = 0;
    uint8_t  order  = ORDER_COLUMNS;
    std::vector<uint8_t> data;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace mcppeserver::network::protocol
