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
// TakeItemEntityPacket — server→client pickup animation. Tells the
// client to play "entity target flies into entity entityId" animation.
// Used to visualize a player picking up a dropped item.
//
// PMMP wire format (protocol 84) — TakeItemEntityPacket::encode:
//   [1 byte ID 0x11]
//   [Int64 BE] target     entity being picked up (the dropped item)
//   [Int64 BE] entityId   entity picking up (the player)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class TakeItemEntityPacket : public DataPacket {
public:
    uint8_t pid() const override { return TAKE_ITEM_ENTITY_PACKET; }

    int64_t target    = 0;   // entity ID of the dropped item
    int64_t entityId  = 0;   // entity ID of the player picking up

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
