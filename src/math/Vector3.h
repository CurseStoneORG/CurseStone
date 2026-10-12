// SPDX-License-Identifier: LGPL-3.0-or-later
// Vector3 — 3D vector. Pure C struct + helpers. Heavily used by
// entity position, level coordinates, particle spawning, and
// packet fields (MoveEntityPacket, MovePlayerPacket, etc.).
#pragma once

#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
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
    return mcp_v3(a.y * b.z - a.z * b.y,
                  a.z * b.x - a.x * b.z,
                  a.x * b.y - a.y * b.x);
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
    if (len > 0.0) return mcp_v3_mul(a, 1.0 / len);
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
    if (deg < 0) deg += 360.0;
    return deg;
}
static inline double mcp_v3_get_pitch(mcp_vector3_t dir) {
    double xz = sqrt(dir.x * dir.x + dir.z * dir.z);
    double pitch = -atan2(dir.y, xz);
    double deg = pitch * 180.0 / M_PI;
    return deg;
}

#ifdef __cplusplus
} // extern "C"
#endif
