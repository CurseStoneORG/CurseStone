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
// ContainerOpenPacket — server->client "show this container UI" packet.
//
// PMMP wire format (protocol 84) — ContainerOpenPacket::encode
// (PocketMine-MP-4.0.0/src/pocketmine/network/protocol/ContainerOpenPacket.php,
// the legacy protocol-84 variant attached as design reference):
//   putByte(windowid)
//   putByte(type)        // container "window type" (0 = chest)
//   putShort(slots)      // slot count (27 for a single chest)
//   putInt(x)
//   putInt(y)
//   putInt(z)
//   putLong(entityId)    // -1 when the container is block-based, not
//                        // entity-based (a chest, not e.g. a horse)
//
// This project only opens chests today, so `type`/`entityId` have a
// single real value in practice (TYPE_CONTAINER / -1), but both are
// left as real fields (not hardcoded constants inside encode()) so a
// future furnace/hopper/entity-inventory packet can reuse this class
// without another rewrite.
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

    // Window "type" byte — PMMP's WindowTypes table. Only the ones this
    // project actually opens are named here; add more as they're needed.
    static constexpr uint8_t TYPE_CONTAINER = 0;
    static constexpr uint8_t TYPE_FURNACE = 2; // WORKBENCH=1, FURNACE=2

    uint8_t windowid = 0;
    uint8_t type = TYPE_CONTAINER;
    int16_t slots = 0;
    int32_t x = 0, y = 0, z = 0;
    int64_t entityId = -1;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
