/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
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
// Sends a non-player entity spawn packet to the client.
//
// Protocol 84 wire format:
//   [Long  BE] Entity ID
//   [Int   BE] Entity type
//   [Float BE] Position (x, y, z)
//   [Float BE] Velocity (x, y, z)
//   [Float BE] Yaw
//   [Float BE] Pitch
//   [Int   BE] Modifiers
//   [Metadata] Entity metadata
//   [Short BE] Link count and links
#pragma once

#include "DataPacket.h"
#include "SetEntityDataPacket.h" // Entity metadata entries.
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class AddEntityPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return ADD_ENTITY_PACKET;
    }

    // Entity network IDs for protocol 84.
    static constexpr int32_t TYPE_LIGHTNING = 93;
    static constexpr int32_t TYPE_ARROW = 80;
    static constexpr int32_t TYPE_THROWN_POTION = 86;
    static constexpr int32_t TYPE_FALLING_BLOCK = 66;

    int64_t eid = 0;
    int32_t type = 0;
    float x = 0, y = 0, z = 0;
    float speedX = 0, speedY = 0, speedZ = 0;
    float yaw = 0, pitch = 0;
    int32_t modifiers = 0;
    bool yawPitchScaled = true; // Scale yaw and pitch by 0.71111.
    // Optional metadata encoded as a type/index key, value, and 0x7F terminator.
    // Falling blocks include DATA_BLOCK_INFO (20) to identify the rendered block.
    // Empty metadata preserves the single-byte 0x00 payload used by other entities.
    std::vector<SetEntityDataPacket::Entry> metadata;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol