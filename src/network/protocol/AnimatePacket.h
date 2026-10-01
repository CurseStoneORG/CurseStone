// SPDX-License-Identifier: LGPL-3.0-or-later
// AnimatePacket.h — client→server arm swing / animation trigger.
//
// MCPE protocol 84 (MCPE 0.15.10) wire format — Genisys
// AnimatePacket::decode (iTXTech/Genisys, src/pocketmine/network/
// protocol/AnimatePacket.php):
//
//   [1 byte   ID 0x27]
//   [VarInt]  action — getVarInt() (1 = swing arm)
//   [Long BE] eid    — getEntityId()
//
// The client sends this on every arm swing (punching, mining, placing).
// Server-side we broadcast it to nearby players so they see each other's
// arm swings (same behaviour as Genisys Player::processMovement's
// ANIMATE_PACKET handling).
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

// PMMP AnimatePacket action constants.
constexpr uint32_t ANIMATE_ACTION_SWING_ARM = 1;

class AnimatePacket : public DataPacket {
public:
    uint8_t pid() const override { return ANIMATE_PACKET; }

    uint32_t action = ANIMATE_ACTION_SWING_ARM;
    int64_t  eid = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
