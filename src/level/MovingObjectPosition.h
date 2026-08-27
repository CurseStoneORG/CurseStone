// SPDX-License-Identifier: LGPL-3.0-or-later
// MovingObjectPosition.h — ported from cn.nukkit.level.MovingObjectPosition
//
// Represents the result of a ray-trace: either a block hit (with
// coordinates + side) or an entity hit.
#pragma once
#include "math/Vector3.h"
#include <cstdint>
namespace atomikpe { class Entity; }

namespace atomikpe::level {

class MovingObjectPosition {
public:
    // 0 = block, 1 = entity
    int typeOfHit = 0;

    int32_t blockX = 0, blockY = 0, blockZ = 0;

    // Which side was hit. -1 = full length. 0=bottom, 1=top,
    // 2=east, 3=west, 4=north, 5=south.
    int sideHit = -1;

    math::Vector3 hitVector{0, 0, 0};

    // Only valid when typeOfHit == 1.
    atomikpe::Entity* entityHit = nullptr;

    static MovingObjectPosition fromBlock(int x, int y, int z, int side,
                                            const math::Vector3& hit) {
        MovingObjectPosition r;
        r.typeOfHit = 0;
        r.blockX = x; r.blockY = y; r.blockZ = z;
        r.sideHit = side;
        r.hitVector = hit;
        return r;
    }

    static MovingObjectPosition fromEntity(atomikpe::Entity* e,
                                            double x, double y, double z) {
        MovingObjectPosition r;
        r.typeOfHit = 1;
        r.entityHit = e;
        r.hitVector = math::Vector3{x, y, z};
        return r;
    }
};

} // namespace atomikpe::level
