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
#include "Tile.h"
#include "Spawnable.h"
#include <string>
namespace cursestone::tile {
class TileSpawner : public Tile, public Spawnable {
public:
    TileSpawner(cursestone::world::Position pos) : Tile("MobSpawner", pos) {}
    void spawnTo(void* /*p*/) override {}
    int spawnDelay() const { return delay_; }
    void setDelay(int d) { delay_ = d; dirty_ = true; }
    const std::string& entityId() const { return entity_id_; }
    void setEntityId(std::string id) { entity_id_ = std::move(id); dirty_ = true; }
private:
    int delay_ = 200;
    std::string entity_id_;
};
} // namespace
