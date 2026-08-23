// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEventPacket — broadcast particles + sounds to nearby players.
//
// PMMP wire format (protocol 84) — LevelEventPacket::encode():
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
// a garbage evid, which is exactly the "particles don't show up" bug.
//
// PMMP LevelEvent constants (verbatim from LevelEventPacket.php):
//   1000..1052  sounds (click, door, fizz, anvil, ..., BLOCK_PLACE=1052)
//   2000..2004  particles (SHOOT, DESTROY, SPLASH, EYE_DESPAWN, SPAWN)
//   3001..3004  weather (start/stop rain/thunder)
//   3500..3507  misc sounds (button, explode, spell, splash)
//
// IMPORTANT (from PMMP source comment in DestroyBlockParticle.php):
//   "NOTE: This is also the destroy block SOUND."
// On protocol 84 there is NO separate break-sound packet — sending a single
// LevelEventPacket with evid=EVENT_PARTICLE_DESTROY (2001) plays BOTH the
// particle effect AND the dig sound at the client.
//
// Data encoding (verbatim from PMMP):
//   - For EVENT_PARTICLE_DESTROY: data = blockId | (blockMeta << 12)
//     (blockId in low 8 bits, blockMeta in bits 12..15)
//   - For EVENT_SOUND_BLOCK_PLACE: data = blockId  (no meta bits)
//
// Position encoding (verbatim from PMMP):
//   - For break particle: BLOCK CENTER  = x+0.5, y+0.5, z+0.5
//   - For place sound:    BLOCK CORNER  = x, y, z  (no +0.5)
//
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::network::protocol {

class LevelEventPacket : public DataPacket {
public:
    uint8_t pid() const override { return LEVEL_EVENT_PACKET; }

    // ---- Sound event ids (PMMP LevelEventPacket::EVENT_SOUND_*) ----
    static constexpr int16_t EVENT_SOUND_CLICK            = 1000;
    static constexpr int16_t EVENT_SOUND_CLICK_FAIL       = 1001;
    static constexpr int16_t EVENT_SOUND_SHOOT            = 1002;
    static constexpr int16_t EVENT_SOUND_DOOR             = 1003;
    static constexpr int16_t EVENT_SOUND_FIZZ            = 1004;
    static constexpr int16_t EVENT_SOUND_TNT             = 1005;
    static constexpr int16_t EVENT_SOUND_GHAST           = 1007;
    static constexpr int16_t EVENT_SOUND_GHAST_SHOOT     = 1008;
    static constexpr int16_t EVENT_SOUND_BLAZE_SHOOT     = 1009;
    static constexpr int16_t EVENT_SOUND_DOOR_BUMP       = 1010;
    static constexpr int16_t EVENT_SOUND_DOOR_CRASH      = 1012;
    static constexpr int16_t EVENT_SOUND_BAT_FLY         = 1015;
    static constexpr int16_t EVENT_SOUND_ZOMBIE_INFECT   = 1016;
    static constexpr int16_t EVENT_SOUND_ZOMBIE_HEAL     = 1017;
    static constexpr int16_t EVENT_SOUND_ENDERMAN_TELEPORT = 1018;
    static constexpr int16_t EVENT_SOUND_ANVIL_BREAK     = 1020;
    static constexpr int16_t EVENT_SOUND_ANVIL_USE       = 1021;
    static constexpr int16_t EVENT_SOUND_ANVIL_FALL       = 1022;
    static constexpr int16_t EVENT_SOUND_DROP_ITEM       = 1030;
    static constexpr int16_t EVENT_SOUND_THROW_PROJECTILE = 1031;
    static constexpr int16_t EVENT_SOUND_ITEMFRAME_ADD_ITEM = 1040;
    static constexpr int16_t EVENT_SOUND_ITEMFRAME_PLACE  = 1041;
    static constexpr int16_t EVENT_SOUND_ITEMFRAME_DROP_ITEM = 1043;
    static constexpr int16_t EVENT_SOUND_ITEMFRAME_ROTATE_ITEM = 1044;
    static constexpr int16_t EVENT_SOUND_EXP_PICKUP       = 1051;
    static constexpr int16_t EVENT_SOUND_BLOCK_PLACE      = 1052;  // <-- block place sound
    static constexpr int16_t EVENT_SOUND_BUTTON_CLICK    = 3500;
    static constexpr int16_t EVENT_SOUND_EXPLODE         = 3501;
    static constexpr int16_t EVENT_SOUND_SPELL           = 3504;
    static constexpr int16_t EVENT_SOUND_SPLASH          = 3506;
    static constexpr int16_t EVENT_SOUND_GRAY_SPLASH      = 3507;

    // ---- Particle event ids (PMMP LevelEventPacket::EVENT_PARTICLE_*) ----
    static constexpr int16_t EVENT_PARTICLE_SHOOT        = 2000;
    static constexpr int16_t EVENT_PARTICLE_DESTROY      = 2001;  // <-- break particle + sound
    static constexpr int16_t EVENT_PARTICLE_SPLASH       = 2002;
    static constexpr int16_t EVENT_PARTICLE_EYE_DESPAWN  = 2003;
    static constexpr int16_t EVENT_PARTICLE_SPAWN        = 2004;

    // ---- Weather event ids ----
    static constexpr int16_t EVENT_START_RAIN            = 3001;
    static constexpr int16_t EVENT_START_THUNDER          = 3002;
    static constexpr int16_t EVENT_STOP_RAIN             = 3003;
    static constexpr int16_t EVENT_STOP_THUNDER          = 3004;

    // ---- Misc ----
    static constexpr int16_t EVENT_SET_DATA              = 4000;
    static constexpr int16_t EVENT_PLAYERS_SLEEPING      = 9800;
    static constexpr int16_t EVENT_ADD_PARTICLE_MASK     = 0x4000;

    // Packet fields. `evid` is int16 because PMMP puts it via putShort.
    int16_t  evid = 0;
    float    x = 0, y = 0, z = 0;
    int32_t  data = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
