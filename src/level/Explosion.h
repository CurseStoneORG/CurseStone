// Explosion — block-tnt/creeper blast. Damages nearby entities and
// destroys blocks in a sphere of `size` blocks. Implements the same
// block-impact pattern as PocketMine (sphere raymarch + finite block
// resistance). TNT damage = 4f * power (capped by maxResistance).
#pragma once
#include "math/Vector3.h"
#include "level/Position.h"
#include <vector>
#include <cstdint>
namespace cursestone::level {
class Explosion {
public:
    Explosion(Position pos, float power)
        : pos_(pos), power_(power) {}
    // Computes the affected blocks and removes them. Returns the
    // count of blocks destroyed.
    int explode();
    float power() const { return power_; }
    const Position& position() const { return pos_; }
private:
    Position pos_;
    float power_;
};
} // namespace
