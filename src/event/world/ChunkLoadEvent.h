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
// ChunkLoadEvent — world event.
#pragma once
#include "event/Event.h"
#include <string>
namespace cursestone::world {
class World;
}
namespace cursestone::event::world {
class ChunkLoadEvent : public Event {
  public:
    cursestone::world::World* world;
    int chunk_x = 0, chunk_z = 0;
    ChunkLoadEvent(cursestone::world::World* l = nullptr, int x = 0, int z = 0)
        : world(l), chunk_x(x), chunk_z(z) {
    }
    const std::string& eventName() const override {
        static std::string n = "ChunkLoadEvent";
        return n;
    }
};
} // namespace
