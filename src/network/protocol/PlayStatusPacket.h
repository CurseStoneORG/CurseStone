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
// PlayStatusPacket.h — server→client status signal during login/spawn.
//
// Wire format (protocol 84):
//   [1 byte ID 0x02]
//   [Int32 BE] status
//
// Status constants (mirror PMMP):
//   LOGIN_SUCCESS        = 0
//   LOGIN_FAILED_CLIENT  = 1  (client outdated)
//   LOGIN_FAILED_SERVER  = 2  (server outdated)
//   PLAYER_SPAWN         = 3  (tell client to show the world)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class PlayStatusPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return PLAY_STATUS_PACKET;
    }

    // Status enum (PMMP constants).
    static constexpr int32_t LOGIN_SUCCESS = 0;
    static constexpr int32_t LOGIN_FAILED_CLIENT = 1;
    static constexpr int32_t LOGIN_FAILED_SERVER = 2;
    static constexpr int32_t PLAYER_SPAWN = 3;

    int32_t status = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
