// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEventPacket — broadcast particles + sounds to nearby players.
//
// PMMP wire format (protocol 84) — LevelEventPacket::encode:
//   [1 byte ID 0x16]
//   [Int32 BE] evid   event code (sound id or particle id, see below)
//   [Float BE] x      position
//   [Float BE] y
//   [Float BE] z
//   [Int32 BE] data   block id for terrain particles, 0 for sounds
//
// PMMP event code ranges (LevelEventPacket / LevelEvent):
//   0x0000..0x7FFF  particles (positive int32)
//   0x4000..0x7FFF  sound events (negative int32 in newer protocols;
//                   in protocol 84 PMMP uses POSITIVE ids 1000..1020)
//   0x8000..0xFFFF  events
//
// Block break uses:
//   - particle: PARTICLE_TERRAIN (14) with data = blockId << 8 | blockData
//   - sound:    SOUND_BREAK   (2001) with data = blockId << 8 | blockData
//     (PMMP `LevelSoundEvent` was not yet split off in protocol 84;
//      block-break sound goes through LevelEventPacket with evid 2001.)
//
// All multi-byte fields are big-endian.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::network::protocol {

class LevelEventPacket : public DataPacket {
public:
    uint8_t pid() const override { return LEVEL_EVENT_PACKET; }

    // ---- Particle event ids (PMMP LevelEventPacket::TYPE_*) ----
    // We only need the ones used for block break/place feedback here.
    static constexpr int32_t EVENT_PARTICLE_TERRAIN = 14;   // destroy block
    static constexpr int32_t EVENT_PARTICLE_PUNCH_BLOCK = 22;
    // ---- Sound event ids (PMMP LevelEventPacket::TYPE_*) ----
    static constexpr int32_t EVENT_SOUND_CLICK       = 1000;
    static constexpr int32_t EVENT_SOUND_DOOR        = 1004;
    static constexpr int32_t EVENT_SOUND_FIZZ       = 1005;
    static constexpr int32_t EVENT_SOUND_BREAK      = 2001;   // block break
    static constexpr int32_t EVENT_SOUND_PLACE      = 2002;   // block place

    int32_t  evid = 0;
    float    x = 0, y = 0, z = 0;
    int32_t  data = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
