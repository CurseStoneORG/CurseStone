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
// AddPlayerPacket.h — server→clients broadcast when a player enters view.
//
// Wire format (protocol 84) — PMMP AddPlayerPacket::encode:
//   [1 byte ID 0x0a]
//   [16 bytes] uuid           (putUUID — raw 128-bit UUID bytes, LE for MCPE)
//   [BE String] username
//   [Int64 BE] eid
//   [Float BE] x, y, z
//   [Float BE] speedX, speedY, speedZ
//   [Float BE] yaw, headYaw (= yaw), pitch
//   [Slot]    item    (we send an empty slot for now: id=0)
//   [Metadata] metadata  (we send an empty metadata blob: count=0)
//
// Note: PMMP writes UUID as the raw 16 bytes from UUID::toBinary().
// MCPE uses little-endian byte order for the UUID, so we store the
// 16 bytes from the string-form UUID accordingly.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>
#include <string>
#include <array>

namespace cursestone::network::protocol {

class AddPlayerPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return ADD_PLAYER_PACKET;
    }

    std::array<uint8_t, 16> uuid{}; // 16 raw UUID bytes (MCPE LE order)
    std::string username;
    int64_t eid = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float speedX = 0.0f;
    float speedY = 0.0f;
    float speedZ = 0.0f;
    float yaw = 0.0f;
    float headYaw = 0.0f;
    float pitch = 0.0f;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
