// SPDX-License-Identifier: LGPL-3.0-or-later
#include "physics/Physics.h"

namespace cursestone::physics {

MotionResult sweep(mcp_aabb_t box, mcp_vector3_t motion,
                   const std::vector<BlockBox>& boxes) {
    MotionResult r;

    double dx = motion.x, dy = motion.y, dz = motion.z;
    mcp_aabb_t bb = box;

    // Y first, then X, then Z — Entity::move()'s order, and each pass
    // works on the box as the previous pass left it (the PHP original
    // mutates the receiver in place). Skipping that intermediate offset
    // made the port clip the last two axes against coordinates the box
    // no longer had: items sank into floors and stopped short of walls.
    for (const BlockBox& c : boxes) dy = mcp_aabb_clip_y_collide(bb, c.box, dy);
    bb = mcp_aabb_move(bb, 0.0, dy, 0.0);

    for (const BlockBox& c : boxes) dx = mcp_aabb_clip_x_collide(bb, c.box, dx);
    bb = mcp_aabb_move(bb, dx, 0.0, 0.0);

    for (const BlockBox& c : boxes) dz = mcp_aabb_clip_z_collide(bb, c.box, dz);

    r.motion = mcp_v3(dx, dy, dz);
    r.clipped_x = (dx != motion.x);
    r.clipped_y = (dy != motion.y);
    r.clipped_z = (dz != motion.z);
    // Entity::checkGroundState(): landing means the downward move was
    // clipped. An upward move that got clipped into a ceiling is not
    // "on the ground", which is why the sign matters and not just the
    // clipping.
    r.on_ground = r.clipped_y && motion.y < 0.0;
    return r;
}

mcp_hit_result_t clipRay(mcp_vector3_t from, mcp_vector3_t to,
                         const std::vector<BlockBox>& boxes) {
    mcp_hit_result_t best = mcp_hit_none();
    double best_dist = 0.0;

    for (const BlockBox& c : boxes) {
        mcp_hit_result_t h = mcp_aabb_clip(c.box, from, to);
        if (!mcp_hit_is_hit(&h)) continue;
        double d = mcp_v3_dist_sq(from, h.pos);
        if (mcp_hit_is_hit(&best) && d >= best_dist) continue;
        h.x = c.x; h.y = c.y; h.z = c.z;
        best = h;
        best_dist = d;
    }
    return best;
}

} // namespace cursestone::physics
