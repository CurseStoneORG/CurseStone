// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateBlockPacket.h — server→client block change broadcast.
//
// Wire format (protocol 84) — Genisys 0.15.0 UpdateBlockPacket::encode:
//
//   foreach($this->records as $r){          // x, z, y, blockId, blockData, flags
//       $this->putInt($r[0]);               // x
//       $this->putInt($r[1]);               // z
//       $this->putByte($r[2]);              // y
//       $this->putByte($r[3]);              // blockId
//       $this->putByte(($r[5] << 4) | $r[4]); // blockData | flags  <-- ONE byte
//   }
//
// *** flags AND blockData share the SAME byte ***
//
// This project used to write them as two separate bytes
// (putByte(blockData); putByte(flags)), which produced a body one byte
// too long and — much worse — put a *different value in every field*:
//
//   * the client read the packed byte (our blockData) and derived
//     flags = byte >> 4, data = byte & 0x0F. For a break we send
//     blockData = 0, so the client derived flags = 0x0 = FLAG_NONE:
//     no neighbour rebuild, no network/mesh update. The receiving
//     client kept rendering the destroyed block and — because its
//     collision mesh was never rebuilt — let players walk straight
//     through it ("ghost block"). Only the breaker saw the change,
//     because their own client predicts block edits locally.
//   * for any block whose damage value was >= 16 the extra byte shifted
//     nothing useful either, it was simply appended past the fields the
//     client parses.
//
// Flag values themselves follow Genisys, which uses FLAG_ALL_PRIORITY
// (0x0B) for both useBreakOn (Level.php:1525) and block placement
// (Level.php:1628).
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class UpdateBlockPacket : public DataPacket {
public:
    uint8_t pid() const override { return UPDATE_BLOCK_PACKET; }

    // Flag bits (Genisys UpdateBlockPacket constants).
    static constexpr uint8_t FLAG_NONE      = 0b0000;
    static constexpr uint8_t FLAG_NEIGHBORS = 0b0001;
    static constexpr uint8_t FLAG_NETWORK   = 0b0010;
    static constexpr uint8_t FLAG_NOGRAPHIC = 0b0100;
    static constexpr uint8_t FLAG_PRIORITY  = 0b1000;

    static constexpr uint8_t FLAG_ALL          = (FLAG_NEIGHBORS | FLAG_NETWORK);          // 0x03
    static constexpr uint8_t FLAG_ALL_PRIORITY = (FLAG_ALL | FLAG_PRIORITY);              // 0x0B

    int32_t x = 0, z = 0;
    uint8_t y = 0;
    uint8_t blockId = 0;
    // Only the low 4 bits travel on the wire — MCPE block damage is 0..15.
    uint8_t blockData = 0;
    // Default matches Genisys' break/place path (FLAG_ALL_PRIORITY) so a
    // call site that forgets to set it still produces a visible update.
    uint8_t flags = FLAG_ALL_PRIORITY;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol