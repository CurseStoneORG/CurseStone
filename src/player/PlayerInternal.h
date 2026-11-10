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
#pragma once

// Shared helpers for Player implementation files.
#include "network/protocol/SetEntityDataPacket.h"

namespace cursestone {

// Player eye height used to convert between eye and feet coordinates.
constexpr double kEyeHeight = 1.62;

// Compose the pose flags byte for DATA_FLAGS (protocol 84): bit 0 ONFIRE,
// 1 SNEAKING, 3 SPRINTING, 4 ACTION (bow draw), 5 INVISIBLE. Callers must
// always send the whole byte — the receiving client replaces the property.
inline uint8_t poseFlagByte(bool sneaking, bool sprinting, bool action = false,
                            bool onFire = false, bool invisible = false) {
    using Set = network::protocol::SetEntityDataPacket;
    uint8_t flags = 0;
    if (onFire)
        flags |= (1 << Set::FLAG_BIT_ONFIRE);
    if (sneaking)
        flags |= (1 << Set::FLAG_BIT_SNEAKING);
    if (sprinting)
        flags |= (1 << Set::FLAG_BIT_SPRINTING);
    if (action)
        flags |= (1 << Set::FLAG_BIT_ACTION);
    if (invisible)
        flags |= (1 << Set::FLAG_BIT_INVISIBLE);
    return flags;
}

// Build the metadata entry for player pose flags.
inline network::protocol::SetEntityDataPacket::Entry
makePoseFlagEntry(bool sneaking, bool sprinting, bool action = false, bool onFire = false,
                  bool invisible = false) {
    network::protocol::SetEntityDataPacket::Entry e;
    e.index = network::protocol::SetEntityDataPacket::PROP_FLAGS;
    e.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    e.ival = poseFlagByte(sneaking, sprinting, action, onFire, invisible);
    return e;
}
} // namespace cursestone