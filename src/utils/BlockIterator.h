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
// BlockIterator — voxel ray-tracer. Steps along a Vector3 direction,
// visiting each block coordinate in order. Used for player targeting
// (block break/place selection) and projectile collision.
#pragma once

#include "CurseStone.h"
#include <cstdint>

namespace cursestone::utils {

class BlockIterator {
  public:
    BlockIterator(const math::Vector3& start, const math::Vector3& direction, double maxDistance);

    // Advance to the next voxel boundary. Returns false if maxDistance exceeded.
    bool next();

    int currentX() const {
        return curX_;
    }
    int currentY() const {
        return curY_;
    }
    int currentZ() const {
        return curZ_;
    }
    double traveled() const {
        return traveled_;
    }

  private:
    // Amanatides & Woo's fast voxel traversal.
    double x_, y_, z_;
    double dx_, dy_, dz_;
    int curX_, curY_, curZ_;
    double stepX_, stepY_, stepZ_;
    double tMaxX_, tMaxY_, tMaxZ_;
    double tDeltaX_, tDeltaY_, tDeltaZ_;
    double maxDistance_;
    double traveled_;
};

} // namespace cursestone::utils
