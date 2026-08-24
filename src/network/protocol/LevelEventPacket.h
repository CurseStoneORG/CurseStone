// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEventPacket — broadcast particles + sounds to nearby players.
//
// PMMP wire format (protocol 84) — LevelEventPacket::encode:
//   [1 byte ID 0x16]
//   [Int16 BE] evid   event code (sound id or particle id, see below)
//   [Float BE] x      position
//   [Float BE] y
//   [Float BE] z
//   [Int32 BE] data   event-specific payload
//
// NOTE: PMMP `putShort` is big-endian (pack "n"), so `evid` is a 2-byte
// BE signed short — NOT a 4-byte int. This is critical: encoding evid as
// int32 BE shifts every subsequent field by 2 bytes and the client sees
// a garbage evid, which causes particles/sounds to not show up.
//
// IMPORTANT (from PMMP source comment in DestroyBlockParticle.php):
//   "NOTE: This is also the destroy block SOUND."
// On protocol 84 there is NO separate break-sound packet — sending a single
// LevelEventPacket with evid=EVENT_PARTICLE_DESTROY (2001) plays BOTH the
// particle effect AND the dig sound at the client.
//
// Data encoding (verbatim from PMMP):
//   - For EVENT_PARTICLE_DESTROY: data = blockId | (blockMeta << 12)
//   - For EVENT_SOUND_BLOCK_PLACE: data = blockId  (no meta bits)
//
// Position encoding (verbatim from PMMP):
//   - For break particle: BLOCK CENTER  = x+0.5, y+0.5, z+0.5
//   - For place sound:    BLOCK CORNER  = x, y, z  (no +0.5)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class LevelEventPacket : public DataPacket {
public:
    uint8_t pid() const override { return LEVEL_EVENT_PACKET; }

    // ---- Sound event ids (PMMP LevelEventPacket::EVENT_SOUND_*) ----
    static constexpr int16_t EVENT_SOUND_BLOCK_PLACE      = 1052;
    // ---- Particle event ids (PMMP LevelEventPacket::EVENT_PARTICLE_*) ----
    static constexpr int16_t EVENT_PARTICLE_DESTROY      = 2001;  // break particle + sound

    int16_t  evid = 0;
    float    x = 0, y = 0, z = 0;
    int32_t  data = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
