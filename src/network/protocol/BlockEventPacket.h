/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockEventPacket — MCPE protocol 84 packet.
//
// Server->client block-level event. The 0.15.10 client drives the
// chest lid animation AND the chest open/close sounds from this packet
// addressed to the chest's block position. case1 is the event kind:
// ALWAYS 1 for a chest (the lid/viewer event). case2 is the event
// payload: 2 = lid up (open animation + open sound), 0 = lid down
// (close animation + close sound). Any other case1 is not a chest
// event and the client drops it. This matches the payload the era
// reference servers put on the wire for real 0.15.10 clients
// (the 0.15-era server software) and later the era reference versions.
//
// Wire layout (big-endian, no varints):
//   [Int BE] x
//   [Int BE] y
//   [Int BE] z
//   [Int BE] case1
//   [Int BE] case2
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class BlockEventPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return BLOCK_EVENT_PACKET;
    }

    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    int32_t case1 = 0;
    int32_t case2 = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
