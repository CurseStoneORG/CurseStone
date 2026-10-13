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
// ChunkEvent.h — ported from cn.nukkit.event.level.ChunkEvent
//
// Base class for all chunk-related events. Holds a reference to the
// owning world and an opaque chunk pointer.
#pragma once
#include "WorldEvent.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class ChunkEvent : public WorldEvent {
public:
    void* chunk;

    ChunkEvent(cursestone::world::World* l, void* c)
        : WorldEvent(l), chunk(c) {}

    const std::string& eventName() const override {
        static std::string n = "ChunkEvent"; return n;
    }
};

} // namespace cursestone::event::world
