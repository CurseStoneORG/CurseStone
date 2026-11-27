/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// PathNode — one A* search cell.
//
// Port of <minecraftpe/impl/entity/path/Node.cpp> (MCPE 0.8.1
// decompile). The era fields keep their roles:
//   xCoord/yCoord/zCoord — the cell.
//   g (field_14)         — cost from the start.
//   h (field_18)         — heuristic distance to the goal.
//   f (nodeCost)         — g + h, the heap key.
//   parent (field_20)    — the reconstruct chain.
//   closed (field_24)    — the closed-set flag.
#pragma once
#include <cstdint>
#include <cmath>

namespace cursestone::entity::ai {

struct PathNode {
    int32_t xCoord = 0, yCoord = 0, zCoord = 0;
    float g = 0.0f;
    float h = 0.0f;
    float f = 0.0f;
    PathNode* parent = nullptr;
    bool closed = false;
    int32_t heap_index = -1; ///< Heap position while in the open set
                             ///< (the era nodePositionInHeapMaybe).

    PathNode() = default;
    PathNode(int32_t x, int32_t y, int32_t z)
        : xCoord(x), yCoord(y), zCoord(z) {}

    /// @brief Euclidean distance to another cell (the era Node::distanceTo).
    float distanceTo(const PathNode& other) const {
        const float dx = (float)(xCoord - other.xCoord);
        const float dy = (float)(yCoord - other.yCoord);
        const float dz = (float)(zCoord - other.zCoord);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    /// @brief Era Node::equals: cell identity.
    bool equals(const PathNode& other) const {
        return xCoord == other.xCoord && yCoord == other.yCoord &&
               zCoord == other.zCoord;
    }

    /// @brief Era Node::createHash: the 3D cell packed into 32 bits.
    static uint32_t createHash(int32_t x, int32_t y, int32_t z) {
        return (uint32_t)(x & 0x3FF) | ((uint32_t)(z & 0x3FF) << 10) |
               ((uint32_t)(y & 0x3FF) << 20);
    }
};

} // namespace cursestone::entity::ai
