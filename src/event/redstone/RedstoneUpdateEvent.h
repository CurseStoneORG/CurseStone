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
// RedstoneUpdateEvent.h — ported from cn.nukkit.event.redstone.RedstoneUpdateEvent
//
// Fired when a redstone component receives an update tick. Mirrors
// Nukkit's RedstoneUpdateEvent which simply wraps BlockUpdateEvent.
#pragma once
#include "../block/BlockUpdateEvent.h"
#include <cstdint>
#include <string>

namespace cursestone::event::redstone {

class RedstoneUpdateEvent : public cursestone::event::block::BlockUpdateEvent {
public:
    RedstoneUpdateEvent(int32_t bx, int32_t by, int32_t bz)
        : BlockUpdateEvent(bx, by, bz) {}

    const std::string& eventName() const override {
        static std::string n = "RedstoneUpdateEvent"; return n;
    }
};

} // namespace cursestone::event::redstone
