// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateBlockPacket.h — server→client block change broadcast.
//
// Wire format (protocol 84) — PMMP UpdateBlockPacket::encode:
//   [1 byte ID 0x13]
//   [Int32 BE] x
//   [Int32 BE] z
//   [Uint8]    y         (byte 0..255)
//   [Uint8]    blockId
//   [Uint8]    blockData
//   [Uint8]    flags      (FLAG_NEIGHBORS | FLAG_NETWORK | ...)
//
// Note PMMP's putInt(x)+putInt(z)+putByte(y)+putByte(id)+putByte(data)+putByte(flags)
// ordering, which differs from the order used in newer protocols.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class UpdateBlockPacket : public DataPacket {
public:
    uint8_t pid() const override { return UPDATE_BLOCK_PACKET; }

    // Flag bits (PMMP UpdateBlockPacket constants).
    static constexpr uint8_t FLAG_NONE       = 0;
    static constexpr uint8_t FLAG_NEIGHBORS  = 1 << 0;
    static constexpr uint8_t FLAG_NETWORK    = 1 << 1;
    static constexpr uint8_t FLAG_NO_OBSERVE  = 1 << 2;
    static constexpr uint8_t FLAG_ALL         = 0xFF;

    int32_t x = 0, z = 0;
    uint8_t y = 0;
    uint8_t blockId = 0;
    uint8_t blockData = 0;
    uint8_t flags = FLAG_ALL;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
