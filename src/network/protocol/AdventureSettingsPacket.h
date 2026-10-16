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
// AdventureSettingsPacket.h — flight/pvp/world-builder flags.
//
// Wire format (protocol 84) — the reference server AdventureSettingsPacket::encode:
//   [1 byte ID 0x31]
//   [Int32 BE] flags            (bitmask: bit 0 = worldImmutable,
//                                          bit 1 = noPvP, bit 2 = noMobs,
//                                          bit 3 = worldBuilder? — we send 0)
//                                          The reference server: ACTION_PERMISSION = bit 0x1 (worldImmutable)
//                                          PERMISSION_NORMAL = 0, PERMISSION_VISITOR = 0x1F
//   [Int32 BE] userPermission   (PERMISSION_NORMAL=0, OPERATOR=1, HOST=2)
//   [Int32 BE] globalPermission (same constants)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class AdventureSettingsPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return ADVENTURE_SETTINGS_PACKET;
    }

    // Common flag bits (the reference server Player::sendSettings).
    static constexpr uint32_t FLAG_WORLD_IMMUTABLE = 0x01;
    static constexpr uint32_t FLAG_NO_PVP = 0x02;
    static constexpr uint32_t FLAG_NO_MOBS = 0x04;
    static constexpr uint32_t FLAG_WORLD_BUILDER = 0x08;
    static constexpr uint32_t FLAG_ALLOW_FLIGHT = 0x40;
    static constexpr uint32_t FLAG_FLYING = 0x80;

    // Permission constants.
    static constexpr int32_t PERMISSION_VISITOR = 0x1F;
    static constexpr int32_t PERMISSION_NORMAL = 0;
    static constexpr int32_t PERMISSION_OPERATOR = 1;
    static constexpr int32_t PERMISSION_HOST = 2;

    uint32_t flags = 0;
    int32_t userPermission = PERMISSION_NORMAL;
    int32_t globalPermission = PERMISSION_NORMAL;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
