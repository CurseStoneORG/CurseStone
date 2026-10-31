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
// Vector2 — 2D vector. POD struct + helper functions.
#pragma once

#include <math.h>
#include <stdbool.h>


typedef struct mcp_vector2 {
    double x, y;
} mcp_vector2_t;

static inline mcp_vector2_t mcp_v2(double x, double y) {
    mcp_vector2_t v = {x, y};
    return v;
}

static inline mcp_vector2_t mcp_v2_add(mcp_vector2_t a, mcp_vector2_t b) {
    return mcp_v2(a.x + b.x, a.y + b.y);
}
static inline mcp_vector2_t mcp_v2_sub(mcp_vector2_t a, mcp_vector2_t b) {
    return mcp_v2(a.x - b.x, a.y - b.y);
}
static inline mcp_vector2_t mcp_v2_mul(mcp_vector2_t a, double s) {
    return mcp_v2(a.x * s, a.y * s);
}
static inline double mcp_v2_dot(mcp_vector2_t a, mcp_vector2_t b) {
    return a.x * b.x + a.y * b.y;
}
static inline double mcp_v2_len(mcp_vector2_t a) {
    return sqrt(a.x * a.x + a.y * a.y);
}
static inline double mcp_v2_dist(mcp_vector2_t a, mcp_vector2_t b) {
    return mcp_v2_len(mcp_v2_sub(a, b));
}
static inline mcp_vector2_t mcp_v2_normalize(mcp_vector2_t a) {
    double len = mcp_v2_len(a);
    if (len > 0.0)
        return mcp_v2_mul(a, 1.0 / len);
    return a;
}
static inline bool mcp_v2_eq(mcp_vector2_t a, mcp_vector2_t b) {
    return a.x == b.x && a.y == b.y;
}

