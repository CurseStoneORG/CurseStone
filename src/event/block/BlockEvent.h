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
// BlockEvent.h — ported from cn.nukkit.event.block.BlockEvent
//
// Base class for all block-related events. Holds block coordinates.
#pragma once
#include "../Event.h"
#include <cstdint>
namespace cursestone::event::block {

class BlockEvent : public Event {
public:
    int32_t x, y, z;
    BlockEvent(int32_t bx, int32_t by, int32_t bz) : x(bx), y(by), z(bz) {}
    const std::string& eventName() const override {
        static std::string n = "BlockEvent"; return n;
    }
};

} // namespace cursestone::event::block
