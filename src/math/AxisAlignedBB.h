// SPDX-License-Identifier: LGPL-3.0-or-later
// AxisAlignedBB — 3D axis-aligned bounding box. Pure C struct + helpers.
// Used for entity collision, block hit-testing, projectile ray-casts.
#pragma once

#include "Vector3.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_aabb {
    double minX, minY, minZ;
    double maxX, maxY, maxZ;
} mcp_aabb_t;

static inline mcp_aabb_t mcp_aabb_make(double minX, double minY, double minZ,
                                        double maxX, double maxY, double maxZ) {
    mcp_aabb_t b = {minX, minY, minZ, maxX, maxY, maxZ};
    return b;
}
static inline mcp_aabb_t mcp_aabb_from_corners(mcp_vector3_t a,
                                                 mcp_vector3_t b) {
    mcp_aabb_t out;
    out.minX = (a.x < b.x) ? a.x : b.x;
    out.minY = (a.y < b.y) ? a.y : b.y;
    out.minZ = (a.z < b.z) ? a.z : b.z;
    out.maxX = (a.x > b.x) ? a.x : b.x;
    out.maxY = (a.y > b.y) ? a.y : b.y;
    out.maxZ = (a.z > b.z) ? a.z : b.z;
    return out;
}
static inline double mcp_aabb_width(mcp_aabb_t b)  { return b.maxX - b.minX; }
static inline double mcp_aabb_height(mcp_aabb_t b) { return b.maxY - b.minY; }
static inline double mcp_aabb_depth(mcp_aabb_t b)  { return b.maxZ - b.minZ; }
static inline mcp_vector3_t mcp_aabb_center(mcp_aabb_t b) {
    return mcp_v3((b.minX + b.maxX) * 0.5,
                  (b.minY + b.maxY) * 0.5,
                  (b.minZ + b.maxZ) * 0.5);
}
// True if `b` and `other` overlap (touch is not overlap).
static inline bool mcp_aabb_intersects(mcp_aabb_t b, mcp_aabb_t o) {
    return b.minX < o.maxX && b.maxX > o.minX &&
           b.minY < o.maxY && b.maxY > o.minY &&
           b.minZ < o.maxZ && b.maxZ > o.minZ;
}
// True if `point` lies inside `b`.
static inline bool mcp_aabb_contains_point(mcp_aabb_t b, mcp_vector3_t p) {
    return p.x >= b.minX && p.x <= b.maxX &&
           p.y >= b.minY && p.y <= b.maxY &&
           p.z >= b.minZ && p.z <= b.maxZ;
}
// Compute the intersection ratio (0..1) of `t*p0 + (1-t)*p1` with `b`,
// or -1 if no intersection. Used for ray-cast hits.
double mcp_aabb_ray_intersect(mcp_aabb_t b, mcp_vector3_t p0, mcp_vector3_t p1);

// Grow the box by `pad` in all directions.
static inline mcp_aabb_t mcp_aabb_grow(mcp_aabb_t b, double pad) {
    b.minX -= pad; b.minY -= pad; b.minZ -= pad;
    b.maxX += pad; b.maxY += pad; b.maxZ += pad;
    return b;
}

#ifdef __cplusplus
} // extern "C"
#endif
