/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEventPacket — broadcast particles + sounds to nearby players.
//
// The reference server wire format (protocol 84) — LevelEventPacket::encode:
//   [1 byte ID 0x16]
//   [Int16 BE] evid   event code (sound id or particle id, see below)
//   [Float BE] x      position
//   [Float BE] y
//   [Float BE] z
//   [Int32 BE] data   event-specific payload
//
// NOTE: The reference server `putShort` is big-endian (pack "n"), so `evid` is a 2-byte
// BE signed short — NOT a 4-byte int. This is critical: encoding evid as
// int32 BE shifts every subsequent field by 2 bytes and the client sees
// a garbage evid, which causes particles/sounds to not show up.
//
// IMPORTANT (from the reference server source comment in DestroyBlockParticle):
//   "NOTE: This is also the destroy block SOUND."
// On protocol 84 there is NO separate break-sound packet — sending a single
// LevelEventPacket with evid=EVENT_PARTICLE_DESTROY (2001) plays BOTH the
// particle effect AND the dig sound at the client.
//
// Data encoding (verbatim from the reference server):
//   - For EVENT_PARTICLE_DESTROY: data = blockId | (blockMeta << 12)
//   - For EVENT_SOUND_BLOCK_PLACE: data = blockId  (no meta bits)
//
// Position encoding (verbatim from the reference server):
//   - For break particle: BLOCK CENTER  = x+0.5, y+0.5, z+0.5
//   - For place sound:    BLOCK CORNER  = x, y, z  (no +0.5)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class LevelEventPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return LEVEL_EVENT_PACKET;
    }

    // ---- Sound event ids (the reference server LevelEventPacket::EVENT_SOUND_*) ----
    static constexpr int16_t EVENT_SOUND_BLOCK_PLACE = 1052;
    static constexpr int16_t EVENT_SOUND_SHOOT = 1002; // bow release (the reference server LaunchSound)
    // Door open/close (the reference server level/sound/DoorSound: EVENT_SOUND_DOOR).
    // Sent as a LevelEventPacket at the door block position; data = pitch*1000
    // (the reference server always passes pitch 0 for doors). This is the sound the
    // wooden door makes when it is toggled open/closed — by hand or by
    // redstone — and the only "door sound" of protocol 84.
    static constexpr int16_t EVENT_SOUND_DOOR = 1003;
    // ---- Particle event ids (the reference server LevelEventPacket::EVENT_PARTICLE_*) ----
    static constexpr int16_t EVENT_PARTICLE_DESTROY = 2001; // break particle + sound
    // Generic particle events: evid = EVENT_ADD_PARTICLE_MASK | type, with
    // the type in the low 12 bits (protocol 84 — e.g. critical particles
    // are 0x4000 | 2). data carries the particle payload (scale / color).
    static constexpr int16_t EVENT_ADD_PARTICLE_MASK = 0x4000;
    // ---- Weather event ids (the reference implementation LevelEventPacket, MCPE 0.15.10) ----
    // NOTE: on protocol 84 START_THUNDER is 3002 and STOP_RAIN is 3003 —
    // the intuitive order (start=odd, stop=even) is WRONG here and the
    // client silently ignores mis-numbered events.
    // CRITICAL: the client renders NOTHING unless `data` carries a
    // non-zero strength. The reference server always sends
    //   rain:    mt_rand(90000, 110000)
    //   thunder: mt_rand(30000, 40000)
    // A LevelEventPacket with evid=3001 and data=0 is a no-op on the
    // 0.15.10 client (sky stays clear) — that was the original bug.
    static constexpr int16_t EVENT_START_RAIN = 3001;
    static constexpr int16_t EVENT_START_THUNDER = 3002;
    static constexpr int16_t EVENT_STOP_RAIN = 3003;
    static constexpr int16_t EVENT_STOP_THUNDER = 3004;

    int16_t evid = 0;
    float x = 0, y = 0, z = 0;
    int32_t data = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
