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
#include "world/generator/object/OreGenerator.h"
#include "world/generator/noise/MersenneRandom.h"

#include <cmath>

namespace cursestone::world::generator::object {

using cursestone::world::generator::MersenneRandom;

namespace {
constexpr int kMaxY = 127;
inline bool inXZ(int v) {
    return v >= 0 && v < 16;
}
inline bool inY(int y) {
    return y >= 0 && y <= kMaxY;
}

inline double normalizedSquared(double origin, double radius, int v) {
    double d = (v + 0.5 - origin) / radius;
    return d * d;
}
} // namespace

int OreGenerator::place(mcp_chunk_t* c, int64_t seed, const OreVein& v) {
    // Per (chunk, ore) seed so results are deterministic and independent of
    // the order chunks/veins are generated in.
    int64_t mixed = seed ^ ((int64_t)v.block_id << 40) ^ ((int64_t)c->x * 73856093LL) ^
                    ((int64_t)c->z * 19349663LL);
    MersenneRandom rng(mixed);

    int placed = 0;
    int span = (v.max_y > v.min_y) ? (v.max_y - v.min_y) : 0;

    for (int n = 0; n < v.total; ++n) {
        double sourceX = (double)rng.nextInt(16);
        double sourceZ = (double)rng.nextInt(16);
        double sourceY = (double)(v.min_y + (span > 0 ? (int)rng.nextInt(span) : 0));

        double amount = (double)v.amount;
        float angle = rng.nextFloat() * (float)M_PI;
        double dx1 = sourceX + (sin(angle) * amount / 8.0);
        double dx2 = sourceX - (sin(angle) * amount / 8.0);
        double dz1 = sourceZ + (cos(angle) * amount / 8.0);
        double dz2 = sourceZ - (cos(angle) * amount / 8.0);
        double dy1 = sourceY + (double)rng.nextInt(3) - 2.0;
        double dy2 = sourceY + (double)rng.nextInt(3) - 2.0;

        for (int i = 0; i < v.amount; ++i) {
            double originX = dx1 + (dx2 - dx1) * i / amount;
            double originY = dy1 + (dy2 - dy1) * i / amount;
            double originZ = dz1 + (dz2 - dz1) * i / amount;
            double q = (double)rng.nextFloat() * amount / 16.0;
            double radiusH = (sin((double)i * (double)M_PI / amount) + q + 1.0) / 2.0;
            double radiusV = (sin((double)i * (double)M_PI / amount) + q + 1.0) / 2.0;
            if (radiusH < 0.6)
                radiusH = 0.6;
            if (radiusV < 0.6)
                radiusV = 0.6;

            for (int x = (int)(originX - radiusH); x <= (int)(originX + radiusH); ++x) {
                if (!inXZ(x))
                    continue;
                double sx = normalizedSquared(originX, radiusH, x);
                if (sx >= 1.0)
                    continue;

                for (int y = (int)(originY - radiusV); y <= (int)(originY + radiusV); ++y) {
                    if (!inY(y))
                        continue;
                    double sy = normalizedSquared(originY, radiusV, y);
                    if (sx + sy >= 1.0)
                        continue;

                    for (int z = (int)(originZ - radiusH); z <= (int)(originZ + radiusH); ++z) {
                        if (!inXZ(z))
                            continue;
                        double sz = normalizedSquared(originZ, radiusH, z);
                        if (sx + sy + sz < 1.0 &&
                            mcp_chunk_get_block_id(c, x, y, z) == v.target_id) {
                            mcp_chunk_set_block_id(c, x, y, z, v.block_id);
                            ++placed;
                        }
                    }
                }
            }
        }
    }
    return placed;
}

} // namespace cursestone::world::generator::object
