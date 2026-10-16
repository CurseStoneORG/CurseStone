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
// MovePlayerPacket.h — client→server player movement.
//
// Wire format (protocol 84) — the reference server MovePlayerPacket:
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
    uint8_t pid() const override {
        return MOVE_PLAYER_PACKET;
    }

    static constexpr uint8_t MODE_NORMAL = 0;
    static constexpr uint8_t MODE_RESET = 1;
    static constexpr uint8_t MODE_ROTATION = 2;

    int64_t eid = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float bodyYaw = 0.0f;
    float pitch = 0.0f;
    uint8_t mode = MODE_NORMAL;
    bool onGround = false;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
