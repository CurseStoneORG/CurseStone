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
// PlayerActionPacket.h — client→server action notification (start/abort/stop break).
//
// Wire format (protocol 84) — PMMP PlayerActionPacket::decode:
//   [1 byte ID 0x20]
//   [Int64 BE] eid       (legacy entity id; client echoes its own)
//   [Int32 BE] action    (0=START_BREAK, 1=ABORT_BREAK, 2=STOP_BREAK, ...)
//   [Int32 BE] x
//   [Int32 BE] y
//   [Int32 BE] z
//   [Int32 BE] face
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class PlayerActionPacket : public DataPacket {
public:
    uint8_t pid() const override { return PLAYER_ACTION_PACKET; }

    // Action constants (subset of PMMP PlayerAction:: constants relevant
    // for block breaking on protocol 84).
    static constexpr int32_t ACTION_START_BREAK = 0;
    static constexpr int32_t ACTION_ABORT_BREAK = 1;
    static constexpr int32_t ACTION_STOP_BREAK  = 2;
    // ACTION_RELEASE_ITEM = 5 — releasing a drawn bow (Infinity
    // PlayerActionPacket.php:35 — protocol 84 value; the 22 that later
    // protocol versions use is WRONG here and the client never sends it).
    // Infinity Player.php maps this action to the bow release → Arrow
    // spawn flow.
    static constexpr int32_t ACTION_RELEASE_ITEM = 5;
    // El cliente manda esto cuando el jugador toca el boton de
    // "Respawn" en la pantalla de muerte (Infinity:
    // ACTION_SPAWN_SAME_DIMENSION = 7).
    static constexpr int32_t ACTION_SPAWN_SAME_DIMENSION = 7;
    // Sprint toggle (protocol 84 — Infinity PlayerActionPacket.php:
    // ACTION_START_SPRINT = 9, ACTION_STOP_SPRINT = 10. Values 5/6 are
    // RELEASE_ITEM/STOP_SLEEPING in this protocol — using them here made
    // the server misread those actions and never see real sprints).
    static constexpr int32_t ACTION_START_SPRINT = 9;
    static constexpr int32_t ACTION_STOP_SPRINT  = 10;
    // Crouch toggle (protocol 84 — Genisys 0.15.0 PlayerActionPacket.php:
    // ACTION_START_SNEAK = 11, ACTION_STOP_SNEAK = 12). This is what the
    // 0.15.10 client sends when the player taps the sneak button or holds
    // shift on a touch device; the server has to mirror the pose to every
    // other client or the crouch is invisible to everyone else.
    static constexpr int32_t ACTION_START_SNEAK = 11;
    static constexpr int32_t ACTION_STOP_SNEAK  = 12;

    int64_t eid = 0;
    int32_t action = 0;
    int32_t x = 0, y = 0, z = 0;
    int32_t face = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
