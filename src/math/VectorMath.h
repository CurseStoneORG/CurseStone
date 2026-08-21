// SPDX-License-Identifier: LGPL-3.0-or-later
// VectorMath — higher-level vector operations (rotation, looking at a target).
// Pure C; complements Vector2.h / Vector3.h.
#pragma once

#include "Vector3.h"
#include <math.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Spherical → cartesian direction (yaw, pitch in degrees).
static inline mcp_vector3_t mcp_vm_direction_from_yaw_pitch(
        double yawDeg, double pitchDeg) {
    double yaw   = yawDeg   * M_PI / 180.0;
    double pitch = pitchDeg * M_PI / 180.0;
    double xz    = cos(pitch);
    return mcp_v3(-xz * sin(yaw),
                  -sin(pitch),
                  xz * cos(yaw));
}
// Rotate vector `v` around Y axis by `yaw` degrees.
static inline mcp_vector3_t mcp_vm_rotate_y(mcp_vector3_t v, double yawDeg) {
    double yaw = yawDeg * M_PI / 180.0;
    double c = cos(yaw), s = sin(yaw);
    return mcp_v3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}
// Returns the smaller of two angles, accounting for wrap at 360°.
static inline double mcp_vm_angle_difference(double a, double b) {
    double diff = fmod(a - b + 540.0, 360.0) - 180.0;
    return diff;
}
// Returns the axis-aligned face id (0..5) for a yaw.
//   0 = south (+Z)
//   1 = west  (-X)
//   2 = north (-Z)
//   3 = east  (+X)
static inline int mcp_vm_face_from_yaw(double yawDeg) {
    double y = fmod(yawDeg, 360.0);
    if (y < 0) y += 360.0;
    if (y < 45.0 || y >= 315.0) return 3; // east
    if (y < 135.0) return 0;             // south
    if (y < 225.0) return 1;             // west
    return 2;                            // north
}

#ifdef __cplusplus
} // extern "C"
#endif
