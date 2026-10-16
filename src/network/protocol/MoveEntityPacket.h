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
// MoveEntityPacket.h — server→clients entity teleport/movement broadcast.
//
// Wire format (protocol 84) — the reference server MoveEntityPacket:
//   [1 byte ID 0x0f]
//   [Int64 BE] eid
//   [Float BE] x
//   [Float BE] y
//   [Float BE] z
//   [Uint8]   pitch   (encoded as (pitch / (360.0 / 256)) → byte)
//   [Uint8]   yaw
//   [Uint8]   headYaw
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class MoveEntityPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return MOVE_ENTITY_PACKET;
    }

    int64_t eid = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float headYaw = 0.0f;
    float pitch = 0.0f;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
