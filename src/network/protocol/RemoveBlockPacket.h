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
// RemoveBlockPacket.h — client→server block break confirmation.
//
// Wire format (protocol 84) — PMMP RemoveBlockPacket::decode:
//   [1 byte ID 0x12]
//   [Int64 BE] eid
//   [Int32 BE] x
//   [Int32 BE] z         (note: z, not y — see PMMP order)
//   [Uint8]    y
//
// Yes, MCPE protocol 84 quirks: x and z are Int32 BE, y is a single byte.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class RemoveBlockPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return REMOVE_BLOCK_PACKET;
    }

    int64_t eid = 0;
    int32_t x = 0, z = 0;
    uint8_t y = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
