// Position — a level-relative location with x,y,z + Level reference.
// Mirrors pocketmine/level/Position.php. Used by blocks, entities,
// tile-entities and players to know "where am I, in which world?".
// Uses composition with mcp_vector3_t instead of C struct inheritance.
#pragma once
#include "math/Vector3.h"
#include <memory>
#include <cmath>

namespace cenisyspe::level {
class Level;

class Position {
public:
    double x{0}, y{0}, z{0};

    Position() = default;
    Position(double x, double y, double z, std::shared_ptr<Level> lvl = nullptr)
        : x(x), y(y), z(z), level_(std::move(lvl)) {}

    // Convert to/from the raw C type used by generators and chunk code.
    explicit Position(mcp_vector3_t v) : x(v.x), y(v.y), z(v.z) {}
    mcp_vector3_t toV3() const { return mcp_v3(x, y, z); }

    std::shared_ptr<Level> level() const { return level_; }
    void setLevel(std::shared_ptr<Level> l) { level_ = std::move(l); }

    double distance(const Position& o) const {
        double dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }

    bool operator==(const Position& o) const {
        return x == o.x && y == o.y && z == o.z;
    }

protected:
    std::shared_ptr<Level> level_;
};

} // namespace cenisyspe::level
