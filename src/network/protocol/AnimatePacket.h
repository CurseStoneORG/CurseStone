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
// WAKE_UP = 3 (Infinity event/player/PlayerAnimationEvent.php) — sent to
// the player itself by Player::stopSleep() so the 0.15.10 client leaves
// the bed view.
constexpr uint32_t ANIMATE_ACTION_WAKE_UP = 3;

class AnimatePacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return ANIMATE_PACKET;
    }

    uint32_t action = ANIMATE_ACTION_SWING_ARM;
    int64_t eid = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
