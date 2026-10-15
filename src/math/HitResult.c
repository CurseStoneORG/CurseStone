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
// HitResult.c — the ray/segment vs AABB clip.
#include "HitResult.h"

// AABB::clip() — ported from net.minecraft.world.phys.AABB::clip (2013
// MCPE client) and its PHP descendant AxisAlignedBB::clip().
//
// For each of the box's six planes, find where the segment crosses it
// (Vec3::clipX/clipY/clipZ). A crossing only counts if the point lies
// within the box's OTHER two axes — otherwise the segment merely passed
// beside that face. Among the survivors keep the one closest to the
// segment's start, tie-broken in the client's own order
// (minX, maxX, minY, maxY, minZ, maxZ) so a ray through an exact corner
// still reports the same face here as it does in-game.
mcp_hit_result_t mcp_aabb_clip(mcp_aabb_t b, mcp_vector3_t from, mcp_vector3_t to) {
    // Zero-initialised: each is only READ when its clip flag is true, but
    // initialising them keeps the compiler's flow analysis (and a future
    // reader) from having to prove that.
    mcp_vector3_t xh0 = {0.0, 0.0, 0.0}, xh1 = {0.0, 0.0, 0.0};
    mcp_vector3_t yh0 = {0.0, 0.0, 0.0}, yh1 = {0.0, 0.0, 0.0};
    mcp_vector3_t zh0 = {0.0, 0.0, 0.0}, zh1 = {0.0, 0.0, 0.0};

    bool bx0 = mcp_v3_clip_x(from, to, b.minX, &xh0);
    bool bx1 = mcp_v3_clip_x(from, to, b.maxX, &xh1);
    bool by0 = mcp_v3_clip_y(from, to, b.minY, &yh0);
    bool by1 = mcp_v3_clip_y(from, to, b.maxY, &yh1);
    bool bz0 = mcp_v3_clip_z(from, to, b.minZ, &zh0);
    bool bz1 = mcp_v3_clip_z(from, to, b.maxZ, &zh1);

    // containsX / containsY / containsZ of the client.
    if (bx0 && !(xh0.y >= b.minY && xh0.y <= b.maxY && xh0.z >= b.minZ && xh0.z <= b.maxZ))
        bx0 = false;
    if (bx1 && !(xh1.y >= b.minY && xh1.y <= b.maxY && xh1.z >= b.minZ && xh1.z <= b.maxZ))
        bx1 = false;
    if (by0 && !(yh0.x >= b.minX && yh0.x <= b.maxX && yh0.z >= b.minZ && yh0.z <= b.maxZ))
        by0 = false;
    if (by1 && !(yh1.x >= b.minX && yh1.x <= b.maxX && yh1.z >= b.minZ && yh1.z <= b.maxZ))
        by1 = false;
    if (bz0 && !(zh0.x >= b.minX && zh0.x <= b.maxX && zh0.y >= b.minY && zh0.y <= b.maxY))
        bz0 = false;
    if (bz1 && !(zh1.x >= b.minX && zh1.x <= b.maxX && zh1.y >= b.minY && zh1.y <= b.maxY))
        bz1 = false;

    int32_t face = -1;
    mcp_vector3_t closest = mcp_v3(0.0, 0.0, 0.0);
    double best = 0.0;

    if (bx0) {
        closest = xh0;
        best = mcp_v3_dist_sq(from, xh0);
        face = MCP_FACE_WEST;
    }
    if (bx1 && (face < 0 || mcp_v3_dist_sq(from, xh1) < best)) {
        closest = xh1;
        best = mcp_v3_dist_sq(from, xh1);
        face = MCP_FACE_EAST;
    }
    if (by0 && (face < 0 || mcp_v3_dist_sq(from, yh0) < best)) {
        closest = yh0;
        best = mcp_v3_dist_sq(from, yh0);
        face = MCP_FACE_DOWN;
    }
    if (by1 && (face < 0 || mcp_v3_dist_sq(from, yh1) < best)) {
        closest = yh1;
        best = mcp_v3_dist_sq(from, yh1);
        face = MCP_FACE_UP;
    }
    if (bz0 && (face < 0 || mcp_v3_dist_sq(from, zh0) < best)) {
        closest = zh0;
        best = mcp_v3_dist_sq(from, zh0);
        face = MCP_FACE_NORTH;
    }
    if (bz1 && (face < 0 || mcp_v3_dist_sq(from, zh1) < best)) {
        closest = zh1;
        best = mcp_v3_dist_sq(from, zh1);
        face = MCP_FACE_SOUTH;
    }

    if (face < 0)
        return mcp_hit_none();

    mcp_hit_result_t h;
    h.type = MCP_HIT_TILE;
    h.x = 0;
    h.y = 0;
    h.z = 0; // filled in by the caller (physics::clipRay)
    h.face = face;
    h.pos = closest;
    h.entity = (void*)0;
    h.indirect_hit = false;
    return h;
}
