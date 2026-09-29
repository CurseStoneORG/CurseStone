// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockIterator implementation (Amanatides & Woo's voxel traversal).
#include "BlockIterator.h"

extern "C" {
#  include "../math/Vector3.h"
}

#include <cmath>

namespace cursestone::utils {

BlockIterator::BlockIterator(const math::Vector3& start,
                              const math::Vector3& direction,
                              double maxDistance)
    : maxDistance_(maxDistance), traveled_(0.0) {
    // math::Vector3 is the C struct mcp_vector3_t in the C++ namespace alias.
    // Field access is direct on the struct.
    x_ = start.x; y_ = start.y; z_ = start.z;
    double len = std::sqrt(direction.x * direction.x +
                           direction.y * direction.y +
                           direction.z * direction.z);
    if (len <= 0.0) len = 1.0;
    dx_ = direction.x / len;
    dy_ = direction.y / len;
    dz_ = direction.z / len;
    curX_ = (int)std::floor(x_);
    curY_ = (int)std::floor(y_);
    curZ_ = (int)std::floor(z_);
    stepX_ = (dx_ > 0) ? 1 : (dx_ < 0 ? -1 : 0);
    stepY_ = (dy_ > 0) ? 1 : (dy_ < 0 ? -1 : 0);
    stepZ_ = (dz_ > 0) ? 1 : (dz_ < 0 ? -1 : 0);
    // Distance to first voxel boundary.
    double nextX = (stepX_ > 0) ? (curX_ + 1) : curX_;
    double nextY = (stepY_ > 0) ? (curY_ + 1) : curY_;
    double nextZ = (stepZ_ > 0) ? (curZ_ + 1) : curZ_;
    tMaxX_ = (stepX_ != 0) ? (nextX - x_) / dx_ : 1e30;
    tMaxY_ = (stepY_ != 0) ? (nextY - y_) / dy_ : 1e30;
    tMaxZ_ = (stepZ_ != 0) ? (nextZ - z_) / dz_ : 1e30;
    tDeltaX_ = (stepX_ != 0) ? std::abs(1.0 / dx_) : 1e30;
    tDeltaY_ = (stepY_ != 0) ? std::abs(1.0 / dy_) : 1e30;
    tDeltaZ_ = (stepZ_ != 0) ? std::abs(1.0 / dz_) : 1e30;
}

bool BlockIterator::next() {
    if (tMaxX_ < tMaxY_) {
        if (tMaxX_ < tMaxZ_) {
            if (tMaxX_ > maxDistance_) return false;
            traveled_ = tMaxX_;
            curX_ += (int)stepX_;
            tMaxX_ += tDeltaX_;
        } else {
            if (tMaxZ_ > maxDistance_) return false;
            traveled_ = tMaxZ_;
            curZ_ += (int)stepZ_;
            tMaxZ_ += tDeltaZ_;
        }
    } else {
        if (tMaxY_ < tMaxZ_) {
            if (tMaxY_ > maxDistance_) return false;
            traveled_ = tMaxY_;
            curY_ += (int)stepY_;
            tMaxY_ += tDeltaY_;
        } else {
            if (tMaxZ_ > maxDistance_) return false;
            traveled_ = tMaxZ_;
            curZ_ += (int)stepZ_;
            tMaxZ_ += tDeltaZ_;
        }
    }
    return traveled_ <= maxDistance_;
}

} // namespace cursestone::utils
