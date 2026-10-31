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
// Math — small math helpers.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// Linear interpolation.
static inline double mcp_math_lerp(double a, double b, double t) {
    return a + (b - a) * t;
}
// Clamp.
static inline double mcp_math_clamp(double v, double lo, double hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}
// Wrap to range [0, range).
static inline double mcp_math_wrap(double v, double range) {
    double r = fmod(v, range);
    if (r < 0)
        r += range;
    return r;
}
// Map noise [-1, 1] → [0, 1].
static inline double mcp_math_norm(double v) {
    return 0.5 + 0.5 * v;
}
// Smoothstep: Hermite interpolation.
static inline double mcp_math_smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}
// Fade (Perlin): 6t^5 - 15t^4 + 10t^3
static inline double mcp_math_fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}
// degrees <-> radians
static inline double mcp_math_deg2rad(double d) {
    return d * M_PI / 180.0;
}
static inline double mcp_math_rad2deg(double r) {
    return r * 180.0 / M_PI;
}
// Integer floor division for non-negative operands.
static inline int64_t mcp_math_floor_div(int64_t a, int64_t b) {
    if (b == 0)
        return 0;
    int64_t q = a / b;
    int64_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0)))
        --q;
    return q;
}
// Integer floor modulus for non-negative modulus.
static inline int64_t mcp_math_floor_mod(int64_t a, int64_t b) {
    if (b == 0)
        return 0;
    int64_t r = a % b;
    if (r < 0)
        r += (b > 0) ? b : -b;
    return r;
}

