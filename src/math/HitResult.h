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
// HitResult — the outcome of tracing a ray/segment through the world.
// Ported from net.minecraft.world.phys.HitResult (2013 MCPE client,
// handheld/src/world/phys/HitResult.{h,cpp}).
//
// Face ids are the protocol's own, the same numbers PlayerActionPacket /
// InteractPacket and the client's HitResult::f use:
//
//   0 = DOWN  (-Y)   1 = UP    (+Y)
//   2 = NORTH (-Z)   3 = SOUTH (+Z)
//   4 = WEST  (-X)   5 = EAST  (+X)
//
// mcp_aabb_clip() is the client's AABB::clip(): it tries the six plane
// crossings of the segment and keeps the nearest one that actually lies
// ON the box. Block coordinates are NOT part of that computation in the
// client either (the level's ray-trace fills them from the block it
// chose), so this leaves x/y/z at 0 — physics::clipRay() fills them in
// from the collision box it hit.
#pragma once

#include "AxisAlignedBB.h"
#include <stdbool.h>
#include <stdint.h>


typedef enum mcp_hit_type {
    MCP_HIT_NONE = 0,  // the client's NO_HIT
    MCP_HIT_TILE = 1,  // hit a block
    MCP_HIT_ENTITY = 2 // hit an entity (not produced by the AABB clip)
} mcp_hit_type_t;

enum {
    MCP_FACE_DOWN = 0,
    MCP_FACE_UP = 1,
    MCP_FACE_NORTH = 2,
    MCP_FACE_SOUTH = 3,
    MCP_FACE_WEST = 4,
    MCP_FACE_EAST = 5
};

typedef struct mcp_hit_result {
    mcp_hit_type_t type;
    int32_t x, y, z;   // block coords (MCP_HIT_TILE; 0 until filled in)
    int32_t face;      // MCP_FACE_* , or -1 when type == MCP_HIT_NONE
    mcp_vector3_t pos; // exact intersection point, world space
    void* entity;      // MCP_HIT_ENTITY only; borrowed, never owned
    bool indirect_hit; // the client's HitResult::indirectHit
} mcp_hit_result_t;

static inline mcp_hit_result_t mcp_hit_none(void) {
    mcp_hit_result_t h;
    h.type = MCP_HIT_NONE;
    h.x = 0;
    h.y = 0;
    h.z = 0;
    h.face = -1;
    h.pos = mcp_v3(0.0, 0.0, 0.0);
    h.entity = (void*)0;
    h.indirect_hit = false;
    return h;
}

static inline bool mcp_hit_is_hit(const mcp_hit_result_t* h) {
    return h->type != MCP_HIT_NONE;
}

// AABB::clip(from, to) — segment vs box, or MCP_HIT_NONE. x/y/z stay 0;
// see the note at the top of this header.
mcp_hit_result_t mcp_aabb_clip(mcp_aabb_t box, mcp_vector3_t from, mcp_vector3_t to);

