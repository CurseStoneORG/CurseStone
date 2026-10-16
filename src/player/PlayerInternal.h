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
#pragma once

// Shared helpers for Player implementation files.
#include "network/protocol/SetEntityDataPacket.h"

namespace cursestone {

// Player eye height used to convert between eye and feet coordinates.
constexpr double kEyeHeight = 1.62;

// Build the metadata entry for player pose flags.
inline network::protocol::SetEntityDataPacket::Entry
makePoseFlagEntry(bool sneaking, bool sprinting, bool action = false, bool onFire = false,
                  bool invisible = false) {
    network::protocol::SetEntityDataPacket::Entry e;
    e.index = network::protocol::SetEntityDataPacket::PROP_FLAGS;
    e.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    uint8_t flags = 0;
    if (onFire)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_ONFIRE);
    if (sneaking)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_SNEAKING);
    if (sprinting)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_SPRINTING);
    if (action)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_ACTION);
    if (invisible)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_INVISIBLE);
    e.ival = flags;
    return e;
}
} // namespace cursestone