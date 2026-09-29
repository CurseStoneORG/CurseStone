// SPDX-License-Identifier: LGPL-3.0-or-later
// MovePlayerPacket.h — client→server player movement.
//
// Wire format (protocol 84) — PMMP MovePlayerPacket:
//   [1 byte ID 0x10]
//   [Int64 BE] eid
//   [Float BE] x
//   [Float BE] y
//   [Float BE] z
//   [Float BE] yaw
//   [Float BE] bodyYaw
//   [Float BE] pitch
//   [Uint8]    mode       (0=normal, 1=reset, 2=rotation)
//   [Uint8]    onGround   (0/1)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class MovePlayerPacket : public DataPacket {
public:
    uint8_t pid() const override { return MOVE_PLAYER_PACKET; }

    static constexpr uint8_t MODE_NORMAL   = 0;
    static constexpr uint8_t MODE_RESET    = 1;
    static constexpr uint8_t MODE_ROTATION = 2;

    int64_t eid       = 0;
    float   x         = 0.0f;
    float   y         = 0.0f;
    float   z         = 0.0f;
    float   yaw       = 0.0f;
    float   bodyYaw   = 0.0f;
    float   pitch     = 0.0f;
    uint8_t mode      = MODE_NORMAL;
    bool    onGround  = false;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
