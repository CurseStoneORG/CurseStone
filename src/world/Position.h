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
// Position — a level-relative location with x,y,z + World reference.
// Mirrors pocketmine/level/Position.php. Used by blocks, entities,
// tile-entities and players to know "where am I, in which world?".
// Uses composition with mcp_vector3_t instead of C struct inheritance.
#pragma once
#include "math/Vector3.h"
#include <memory>
#include <cmath>

namespace cursestone::world {
class World;

class Position {
public:
    double x{0}, y{0}, z{0};

    Position() = default;
    Position(double x, double y, double z, std::shared_ptr<World> world = nullptr)
        : x(x), y(y), z(z), world_(std::move(world)) {}

    // Convert to/from the raw C type used by generators and chunk code.
    explicit Position(mcp_vector3_t v) : x(v.x), y(v.y), z(v.z) {}
    mcp_vector3_t toV3() const { return mcp_v3(x, y, z); }

    std::shared_ptr<World> world() const { return world_; }
    void setWorld(std::shared_ptr<World> l) { world_ = std::move(l); }

    double distance(const Position& o) const {
        double dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }

    bool operator==(const Position& o) const {
        return x == o.x && y == o.y && z == o.z;
    }

protected:
    std::shared_ptr<World> world_;
};

} // namespace cursestone::world
