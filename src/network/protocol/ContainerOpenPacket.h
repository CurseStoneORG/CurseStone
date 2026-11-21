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
// ContainerOpenPacket — server->client "show this container UI" packet.
//
// Wire format (protocol 84, the 1.6-era reference implementation ContainerOpenPacket::encode):
//   putByte(windowid)
//   putByte(type)        // container "window type" (0 = chest, 2 = furnace)
//   putShort(slots)      // slot count (27 for a single chest, 3 furnace)
//   putInt(x)
//   putInt(y)
//   putInt(z)
//
// There is NO trailing entityId long in the protocol-84 reference encode
// (the 1.6-era reference implementation). the reference server's 0.15-era packet
// appended one; this encode matches the reference layout. A type=2 open
// binds the furnace screen to the block actor at the coords, so the
// tile's client compound (BurnTime/CookTime/BurnDuration/Items) must
// already sit on the client when the record lands — see
// Player::openFurnaceAt for the open batch order.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class ContainerOpenPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return CONTAINER_OPEN_PACKET;
    }

    // Window "type" byte — the reference server's WindowTypes table. Only the ones this
    // project actually opens are named here; add more as they're needed.
    static constexpr uint8_t TYPE_CONTAINER = 0;
    static constexpr uint8_t TYPE_FURNACE = 2; // WORKBENCH=1, FURNACE=2
    // TYPE_FURNACE is the only window type that draws the actual furnace
    // screen. It binds the screen to the tile compound the chunk stream
    // delivered ahead of the open record (see Player::openFurnaceAt).

    uint8_t windowid = 0;
    uint8_t type = TYPE_CONTAINER;
    int16_t slots = 0;
    int32_t x = 0, y = 0, z = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
