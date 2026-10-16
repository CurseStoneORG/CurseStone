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
// Explosion — block-tnt/creeper blast. Damages nearby entities and
// destroys blocks in a sphere of `size` blocks. Implements the same
// block-impact pattern as the reference server (sphere raymarch + finite block
// resistance). TNT damage = 4f * power (capped by maxResistance).
#pragma once
#include "math/Vector3.h"
#include "world/Position.h"
#include <vector>
#include <cstdint>
namespace cursestone::world {
class Explosion {
  public:
    Explosion(Position pos, float power) : pos_(pos), power_(power) {
    }
    // Computes the affected blocks and removes them. Returns the
    // count of blocks destroyed.
    int explode();
    float power() const {
        return power_;
    }
    const Position& position() const {
        return pos_;
    }

  private:
    Position pos_;
    float power_;
};
} // namespace
