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
#include "world/generator/object/LakeGenerator.h"
#include "block/BlockIds.h"

namespace cursestone::world::generator::object {

using cursestone::block::BlockId;

namespace {
constexpr int kMaxY = 127;
inline bool inXZ(int v) {
    return v >= 0 && v < 16;
}
inline bool inY(int y) {
    return y >= 0 && y <= kMaxY;
}
inline bool solid(uint8_t id) {
    return id != (uint8_t)BlockId::AIR && id != (uint8_t)BlockId::WATER &&
           id != (uint8_t)BlockId::FLOWING_WATER && id != (uint8_t)BlockId::LEAVES &&
           id != (uint8_t)BlockId::LEAVES_2 && id != (uint8_t)BlockId::LOG;
}
} // namespace

bool LakeGenerator::place(mcp_chunk_t* c, int cx, int cy, int cz, uint8_t liquidId,
                          MersenneRandom& rng) {
    if (!inXZ(cx) || !inXZ(cz) || !inY(cy))
        return false;

    int rx = 2 + (int)rng.nextInt(4);
    int rz = 2 + (int)rng.nextInt(4);
    int ry = 2 + (int)rng.nextInt(2);

    double invx = 1.0 / (double)rx;
    double invy = 1.0 / (double)ry;
    double invz = 1.0 / (double)rz;

    bool changed = false;
    for (int dx = -rx; dx <= rx; ++dx) {
        for (int dz = -rz; dz <= rz; ++dz) {
            for (int dy = -ry; dy <= ry; ++dy) {
                double nx = (double)dx * invx;
                double ny = (double)dy * invy;
                double nz = (double)dz * invz;
                if (nx * nx + ny * ny + nz * nz > 1.0)
                    continue;

                int x = cx + dx, y = cy + dy, z = cz + dz;
                if (!inXZ(x) || !inXZ(z) || !inY(y))
                    continue;

                uint8_t cur = mcp_chunk_get_block_id(c, x, y, z);
                // Only convert terrain into liquid in the lower half of the
                // pool; the upper half becomes air so the surface is open.
                if (solid(cur)) {
                    if (dy <= 0) {
                        mcp_chunk_set_block_id(c, x, y, z, liquidId);
                        changed = true;
                    } else {
                        mcp_chunk_set_block_id(c, x, y, z, (uint8_t)BlockId::AIR);
                        changed = true;
                    }
                }
            }
        }
    }
    return changed;
}

} // namespace cursestone::world::generator::object
