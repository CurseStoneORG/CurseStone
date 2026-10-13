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
// StartGamePacket.h — server tells client "world starts now".
//
// Wire format (protocol 84) — PMMP StartGamePacket::encode:
//   [1 byte ID 0x09]
//   [Int32 BE] seed
//   [Uint8  ] dimension (0=overworld, 1=nether, 2=end)
//   [Int32 BE] generator (0=old, 1=infinite, 2=flat)
//   [Int32 BE] gamemode (only low bit matters to client: 0=survival,1=creative)
//   [Int64 BE] eid  (EntityID of the player; PMMP always sends 0)
//   [Int32 BE] spawnX
//   [Int32 BE] spawnY
//   [Int32 BE] spawnZ
//   [Float32 BE] x (player start pos)
//   [Float32 BE] y
//   [Float32 BE] z
//   [Uint8] 1     (unknown flag, always 1)
//   [Uint8] 1     (unknown flag, always 1)
//   [Uint8] 0     (unknown, always 0)
//   [BE String] unknown (level name? usually empty)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>
#include <string>

namespace cursestone::network::protocol {

class StartGamePacket : public DataPacket {
public:
    uint8_t pid() const override { return START_GAME_PACKET; }

    int32_t  seed       = 0;
    uint8_t  dimension  = 0;
    int32_t  generator  = 1;       // 1 = infinite
    int32_t  gamemode   = 0;
    int64_t  eid        = 0;
    int32_t  spawnX     = 0;
    int32_t  spawnY     = 64;
    int32_t  spawnZ     = 0;
    float    x          = 0.0f;
    float    y          = 64.0f;
    float    z          = 0.0f;
    std::string unknown;           // typically empty

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
