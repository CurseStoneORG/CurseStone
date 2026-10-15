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
// Vector3 — 3D vector. Pure C struct + helpers. Heavily used by
// entity position, level coordinates, particle spawning, and
// packet fields (MoveEntityPacket, MovePlayerPacket, etc.).
#pragma once

#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_vector3 {
    double x, y, z;
} mcp_vector3_t;

static inline mcp_vector3_t mcp_v3(double x, double y, double z) {
    mcp_vector3_t v = {x, y, z};
    return v;
}

static inline mcp_vector3_t mcp_v3_add(mcp_vector3_t a, mcp_vector3_t b) {
    return mcp_v3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static inline mcp_vector3_t mcp_v3_sub(mcp_vector3_t a, mcp_vector3_t b) {
    return mcp_v3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static inline mcp_vector3_t mcp_v3_mul(mcp_vector3_t a, double s) {
    return mcp_v3(a.x * s, a.y * s, a.z * s);
}
static inline mcp_vector3_t mcp_v3_cross(mcp_vector3_t a, mcp_vector3_t b) {
    return mcp_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline double mcp_v3_dot(mcp_vector3_t a, mcp_vector3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline double mcp_v3_len(mcp_vector3_t a) {
    return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
static inline double mcp_v3_lensq(mcp_vector3_t a) {
    return a.x * a.x + a.y * a.y + a.z * a.z;
}
static inline double mcp_v3_dist(mcp_vector3_t a, mcp_vector3_t b) {
    return mcp_v3_len(mcp_v3_sub(a, b));
}
static inline mcp_vector3_t mcp_v3_normalize(mcp_vector3_t a) {
    double len = mcp_v3_len(a);
    if (len > 0.0)
        return mcp_v3_mul(a, 1.0 / len);
    return a;
}
static inline bool mcp_v3_eq(mcp_vector3_t a, mcp_vector3_t b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
// Floor (block coordinate) — used to convert world-space to chunk-space.
static inline mcp_vector3_t mcp_v3_floor(mcp_vector3_t a) {
    return mcp_v3(floor(a.x), floor(a.y), floor(a.z));
}
static inline double mcp_v3_get_yaw(mcp_vector3_t dir) {
    double yaw = atan2(-dir.x, dir.z);
    double deg = yaw * 180.0 / M_PI;
    if (deg < 0)
        deg += 360.0;
    return deg;
}
static inline double mcp_v3_get_pitch(mcp_vector3_t dir) {
    double xz = sqrt(dir.x * dir.x + dir.z * dir.z);
    double pitch = -atan2(dir.y, xz);
    double deg = pitch * 180.0 / M_PI;
    return deg;
}

// ---- Segment/plane clipping (net.minecraft.world.phys.Vec3) --------
// clipX/clipY/clipZ: where does the segment a->b cross the plane
// `axis == target`? Writes the crossing point to *out and returns true
// only when the plane really lies between a and b — parallel segments
// (|delta|^2 < 1e-7, the client's own epsilon) and crossings outside
// the [0,1] parameter range are rejected. These three are the only
// primitive the client's AABB::clip() ray test is built from.
static inline bool mcp_v3_clip_x(mcp_vector3_t a, mcp_vector3_t b, double xt, mcp_vector3_t* out) {
    double xd = b.x - a.x, yd = b.y - a.y, zd = b.z - a.z;
    if (xd * xd < 0.0000001)
        return false;
    double d = (xt - a.x) / xd;
    if (d < 0.0 || d > 1.0)
        return false;
    *out = mcp_v3(a.x + xd * d, a.y + yd * d, a.z + zd * d);
    return true;
}
static inline bool mcp_v3_clip_y(mcp_vector3_t a, mcp_vector3_t b, double yt, mcp_vector3_t* out) {
    double xd = b.x - a.x, yd = b.y - a.y, zd = b.z - a.z;
    if (yd * yd < 0.0000001)
        return false;
    double d = (yt - a.y) / yd;
    if (d < 0.0 || d > 1.0)
        return false;
    *out = mcp_v3(a.x + xd * d, a.y + yd * d, a.z + zd * d);
    return true;
}
static inline bool mcp_v3_clip_z(mcp_vector3_t a, mcp_vector3_t b, double zt, mcp_vector3_t* out) {
    double xd = b.x - a.x, yd = b.y - a.y, zd = b.z - a.z;
    if (zd * zd < 0.0000001)
        return false;
    double d = (zt - a.z) / zd;
    if (d < 0.0 || d > 1.0)
        return false;
    *out = mcp_v3(a.x + xd * d, a.y + yd * d, a.z + zd * d);
    return true;
}

// Squared distance — the sqrt-free twin of mcp_v3_dist(), used by the
// ray clip to pick the NEAREST of the six candidate crossings without
// paying for a square root per candidate.
static inline double mcp_v3_dist_sq(mcp_vector3_t a, mcp_vector3_t b) {
    return mcp_v3_lensq(mcp_v3_sub(a, b));
}

// a + (b - a) * t.
static inline mcp_vector3_t mcp_v3_lerp(mcp_vector3_t a, mcp_vector3_t b, double t) {
    return mcp_v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

#ifdef __cplusplus
} // extern "C"
#endif
