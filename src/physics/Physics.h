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
// ============================================================
// Physics.h — the server-side collision engine.
//
// This is the WORLD-FREE half of the physics port: pure geometry over
// the client's net.minecraft.world.phys model. It knows nothing about
// World, blocks or entities — the caller hands it the collision boxes
// once (World::collisionBoxes()) and it answers the two questions the
// client's own physics answer:
//
//   sweep()    — Entity::move():  where does this box end up when pushed
//                by a motion vector through a set of obstacles, and did
//                it land on the ground?
//   clipRay()  — Level::clip()/AABB::clip(): what block does this
//                segment hit, on which face, and exactly where?
//
// Keeping these dependency-free is deliberate: tools/phys_test.cpp links
// ONLY Physics.cpp + the math headers and checks the numbers, instead of
// re-implementing the algorithm next to the real one (the way the
// dropped-item integrator used to be duplicated inline in World.cpp).
// ============================================================
#pragma once

#include "CurseStone.h"   // cursestone::math aliases
#include <cstdint>
#include <vector>
#include "math/AxisAlignedBB.h"
#include "math/HitResult.h"

namespace cursestone::physics {

// A block's collision box in world space, tagged with the block
// coordinates it came from — those coordinates are what makes a ray hit
// usable (breaking/placing/interacting all need the block, not just the
// point). Air and every walk-through block simply never show up here.
struct BlockBox {
    int32_t x = 0, y = 0, z = 0;
    mcp_aabb_t box{};
};

// Result of one swept move. `motion` is the delta that was actually
// applied (each component clipped down to the free gap against whatever
// it ran into). The three `clipped_*` flags are the caller's cue to
// cancel that motion component, exactly as Entity::move() does.
struct MotionResult {
    mcp_vector3_t motion{};
    bool on_ground = false;
    bool clipped_x = false;
    bool clipped_y = false;
    bool clipped_z = false;
};

// Entity::move(box, motion): resolve the displacement against `boxes`
// one axis at a time — Y, then X, then Z, which is the order the client
// uses — mutating the working box between passes so the X pass already
// sees the Y offset (and the Z pass sees both). Behaviour is identical
// to the inline loops that used to live in World::tickDroppedItems().
//
// `on_ground` is Entity::checkGroundState(): true only when the DOWNWARD
// motion was clipped, i.e. `clipped_y && motion.y < 0`.
MotionResult sweep(mcp_aabb_t box, mcp_vector3_t motion,
                   const std::vector<BlockBox>& boxes);

// Nearest block hit along `from` -> `to` (segment, not infinite ray),
// with the block's coordinates and the face that was struck. Returns
// MCP_HIT_NONE when the segment reaches nothing. Ties are resolved by
// distance from `from`; boxes are not sorted by the caller.
mcp_hit_result_t clipRay(mcp_vector3_t from, mcp_vector3_t to,
                         const std::vector<BlockBox>& boxes);

} // namespace cursestone::physics
