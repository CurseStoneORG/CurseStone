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
// AxisAlignedBB — 3D axis-aligned bounding box. POD struct + helpers.
// Used for entity collision, block hit-testing, projectile ray-casts.
#pragma once

#include "Vector3.h"
#include <stdbool.h>


typedef struct mcp_aabb {
    double minX, minY, minZ;
    double maxX, maxY, maxZ;
} mcp_aabb_t;

static inline mcp_aabb_t mcp_aabb_make(double minX, double minY, double minZ, double maxX,
                                       double maxY, double maxZ) {
    mcp_aabb_t b = {minX, minY, minZ, maxX, maxY, maxZ};
    return b;
}
static inline mcp_aabb_t mcp_aabb_from_corners(mcp_vector3_t a, mcp_vector3_t b) {
    mcp_aabb_t out;
    out.minX = (a.x < b.x) ? a.x : b.x;
    out.minY = (a.y < b.y) ? a.y : b.y;
    out.minZ = (a.z < b.z) ? a.z : b.z;
    out.maxX = (a.x > b.x) ? a.x : b.x;
    out.maxY = (a.y > b.y) ? a.y : b.y;
    out.maxZ = (a.z > b.z) ? a.z : b.z;
    return out;
}
static inline double mcp_aabb_width(mcp_aabb_t b) {
    return b.maxX - b.minX;
}
static inline double mcp_aabb_height(mcp_aabb_t b) {
    return b.maxY - b.minY;
}
static inline double mcp_aabb_depth(mcp_aabb_t b) {
    return b.maxZ - b.minZ;
}
static inline mcp_vector3_t mcp_aabb_center(mcp_aabb_t b) {
    return mcp_v3((b.minX + b.maxX) * 0.5, (b.minY + b.maxY) * 0.5, (b.minZ + b.maxZ) * 0.5);
}
// True if `b` and `other` overlap (touch is not overlap).
static inline bool mcp_aabb_intersects(mcp_aabb_t b, mcp_aabb_t o) {
    return b.minX < o.maxX && b.maxX > o.minX && b.minY < o.maxY && b.maxY > o.minY &&
           b.minZ < o.maxZ && b.maxZ > o.minZ;
}
// True if `point` lies inside `b`.
static inline bool mcp_aabb_contains_point(mcp_aabb_t b, mcp_vector3_t p) {
    return p.x >= b.minX && p.x <= b.maxX && p.y >= b.minY && p.y <= b.maxY && p.z >= b.minZ &&
           p.z <= b.maxZ;
}
// Compute the intersection ratio (0..1) of `t*p0 + (1-t)*p1` with `b`,
// or -1 if no intersection. Used for ray-cast hits.

// Grow the box by `pad` in all directions.
static inline mcp_aabb_t mcp_aabb_grow(mcp_aabb_t b, double pad) {
    b.minX -= pad;
    b.minY -= pad;
    b.minZ -= pad;
    b.maxX += pad;
    b.maxY += pad;
    b.maxZ += pad;
    return b;
}

// ---------------------------------------------------------------------
// Swept-move primitives — net.minecraft.world.phys.AABB (2013 MCPE
// client). Entity::move() is built entirely from these four functions
// plus the three clip?Collide() below, so they are the ground truth for
// "where does this box end up after being pushed by (dx,dy,dz)".
// ---------------------------------------------------------------------

// `expand` (Genisys AxisAlignedBB::addCoord): grow the box by the
// offset ON THE SIDE the offset pushes towards, so the result covers
// BOTH the original box and the box translated by (dx,dy,dz). This is
// the region a swept move has to look at.
static inline mcp_aabb_t mcp_aabb_expand(mcp_aabb_t b, double dx, double dy, double dz) {
    double x0 = b.minX, y0 = b.minY, z0 = b.minZ;
    double x1 = b.maxX, y1 = b.maxY, z1 = b.maxZ;
    if (dx < 0)
        x0 += dx;
    if (dx > 0)
        x1 += dx;
    if (dy < 0)
        y0 += dy;
    if (dy > 0)
        y1 += dy;
    if (dz < 0)
        z0 += dz;
    if (dz > 0)
        z1 += dz;
    return mcp_aabb_make(x0, y0, z0, x1, y1, z1);
}

// `cloneMove` (the client's AABB::cloneMove / the original's offset()): plain
// translation of the whole box. This — not expand() — is what the
// collision-cube lookup is fed today, matching the existing item
// physics exactly; expand() is kept for callers that want the full
// swept volume.
static inline mcp_aabb_t mcp_aabb_clone_move(mcp_aabb_t b, double dx, double dy, double dz) {
    return mcp_aabb_make(b.minX + dx, b.minY + dy, b.minZ + dz, b.maxX + dx, b.maxY + dy,
                         b.maxZ + dz);
}

// `move` — same thing, as an in-place mutation (the original
// mutates the receiver and returns it, which is why the caller's Y
// offset is already visible to the X pass).
static inline mcp_aabb_t mcp_aabb_move(mcp_aabb_t b, double dx, double dy, double dz) {
    return mcp_aabb_clone_move(b, dx, dy, dz);
}

// Inset every face by the given amount (negative amounts grow).
static inline mcp_aabb_t mcp_aabb_shrink(mcp_aabb_t b, double dx, double dy, double dz) {
    return mcp_aabb_make(b.minX + dx, b.minY + dy, b.minZ + dz, b.maxX - dx, b.maxY - dy,
                         b.maxZ - dz);
}

// Mean extent over the three axes (client AABB::getSize()). Used to
// rank candidate entities by hitbox size, not for collision.
static inline double mcp_aabb_size(mcp_aabb_t b) {
    return ((b.maxX - b.minX) + (b.maxY - b.minY) + (b.maxZ - b.minZ)) / 3.0;
}

// Overlap that treats a merely TOUCHING face as an overlap, where
// mcp_aabb_intersects() above requires real interpenetration.
static inline bool mcp_aabb_intersects_inner(mcp_aabb_t b, mcp_aabb_t o) {
    return b.minX <= o.maxX && b.maxX >= o.minX && b.minY <= o.maxY && b.maxY >= o.minY &&
           b.minZ <= o.maxZ && b.maxZ >= o.minZ;
}

// The client's AABB::contains(Vec3): STRICT containment, every bound
// exclusive. Deliberately not mcp_aabb_contains_point() above, which is
// inclusive on every face — a ray that grazes a block corner has to
// read differently in the two, and Entity::move() relies on the strict
// one.
static inline bool mcp_aabb_contains_strict(mcp_aabb_t b, mcp_vector3_t p) {
    return p.x > b.minX && p.x < b.maxX && p.y > b.minY && p.y < b.maxY && p.z > b.minZ &&
           p.z < b.maxZ;
}

// ---- per-axis swept clipping ---------------------------------------
// calculateXOffset / calculateYOffset / calculateZOffset (Genisys
// AxisAlignedBB, what Entity::move() clips with). Given the moving box
// `b`, one obstacle `c` and the requested displacement along that axis,
// return the displacement that is actually free: `d` unchanged when the
// move does not push into `c`, or the exact remaining gap up to the
// obstacle's facing side when it does.
//
// Read the branches as "the obstacle is AHEAD of me in the direction I
// am moving": moving +x, `c` must start at or beyond my maxX, and the
// free distance is c.minX - b.maxX. (Note the 2013 leak's AABB.h spells
// these with c's min/max roles swapped — `c.x1 <= x0` — which would clip
// a rightward move against a block behind you. The form below is the one
// the working integrator used and the only one that produces correct
// collisions, so it is the one that is pinned by tools/phys_test.cpp.)
//
// `c` must already overlap `b` on the other two axes; that is what the
// two early-outs test.
static inline double mcp_aabb_clip_x_collide(mcp_aabb_t b, mcp_aabb_t c, double d) {
    if (c.maxY <= b.minY || c.minY >= b.maxY)
        return d;
    if (c.maxZ <= b.minZ || c.minZ >= b.maxZ)
        return d;
    if (d > 0.0 && c.minX >= b.maxX) {
        double gap = c.minX - b.maxX;
        if (gap < d)
            d = gap;
    }
    if (d < 0.0 && c.maxX <= b.minX) {
        double gap = c.maxX - b.minX;
        if (gap > d)
            d = gap;
    }
    return d;
}
static inline double mcp_aabb_clip_y_collide(mcp_aabb_t b, mcp_aabb_t c, double d) {
    if (c.maxX <= b.minX || c.minX >= b.maxX)
        return d;
    if (c.maxZ <= b.minZ || c.minZ >= b.maxZ)
        return d;
    if (d > 0.0 && c.minY >= b.maxY) {
        double gap = c.minY - b.maxY;
        if (gap < d)
            d = gap;
    }
    if (d < 0.0 && c.maxY <= b.minY) {
        double gap = c.maxY - b.minY;
        if (gap > d)
            d = gap;
    }
    return d;
}
static inline double mcp_aabb_clip_z_collide(mcp_aabb_t b, mcp_aabb_t c, double d) {
    if (c.maxX <= b.minX || c.minX >= b.maxX)
        return d;
    if (c.maxY <= b.minY || c.minY >= b.maxY)
        return d;
    if (d > 0.0 && c.minZ >= b.maxZ) {
        double gap = c.minZ - b.maxZ;
        if (gap < d)
            d = gap;
    }
    if (d < 0.0 && c.maxZ <= b.minZ) {
        double gap = c.maxZ - b.minZ;
        if (gap > d)
            d = gap;
    }
    return d;
}

