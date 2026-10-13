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
// AxisAlignedBB.c — slab-method ray intersection (Andrew Woo's algorithm).
#include "AxisAlignedBB.h"

#include <float.h>

double mcp_aabb_ray_intersect(mcp_aabb_t b, mcp_vector3_t p0, mcp_vector3_t p1) {
    mcp_vector3_t dir = mcp_v3_sub(p1, p0);
    double tmin = 0.0;
    double tmax = 1.0;
    double invD;
    // X slab
    if (fabs(dir.x) < 1e-20) {
        if (p0.x < b.minX || p0.x > b.maxX) return -1;
    } else {
        invD = 1.0 / dir.x;
        double t1 = (b.minX - p0.x) * invD;
        double t2 = (b.maxX - p0.x) * invD;
        if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return -1;
    }
    // Y slab
    if (fabs(dir.y) < 1e-20) {
        if (p0.y < b.minY || p0.y > b.maxY) return -1;
    } else {
        invD = 1.0 / dir.y;
        double t1 = (b.minY - p0.y) * invD;
        double t2 = (b.maxY - p0.y) * invD;
        if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return -1;
    }
    // Z slab
    if (fabs(dir.z) < 1e-20) {
        if (p0.z < b.minZ || p0.z > b.maxZ) return -1;
    } else {
        invD = 1.0 / dir.z;
        double t1 = (b.minZ - p0.z) * invD;
        double t2 = (b.maxZ - p0.z) * invD;
        if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return -1;
    }
    return tmin;
}
