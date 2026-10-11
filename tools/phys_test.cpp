// tools/phys_test.cpp
//
// Regression test for the ported physics primitives — the client's
// net.minecraft.world.phys model that now backs the server:
//
//   src/math/AxisAlignedBB.h   expand / cloneMove / shrink / size /
//                              intersectsInner / containsStrict /
//                              clip{Collide,?Collide}
//   src/math/HitResult.h/.c    HitResult + AABB::clip ray trace
//   src/physics/Physics.h/.cpp sweep (= Entity::move) and clipRay
//
// It includes the REAL headers and links the REAL Physics.cpp, so a
// changed formula fails the test instead of silently changing behaviour
// the way the hand-inlined copy in World::tickDroppedItems() could.
//
//   tools/run_phys_test.sh
#include "physics/Physics.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using cursestone::physics::BlockBox;
using cursestone::physics::MotionResult;

static int failures = 0;

static void check(bool ok, const std::string& name) {
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", name.c_str());
    if (!ok) ++failures;
}

static bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

// The unit collision cube of block (x,y,z) — what a stone/dirt block
// contributes.
static BlockBox unitBox(int x, int y, int z) {
    BlockBox b;
    b.x = x; b.y = y; b.z = z;
    b.box = mcp_aabb_make((double)x, (double)y, (double)z,
                          (double)x + 1.0, (double)y + 1.0, (double)z + 1.0);
    return b;
}

// Flat world: every cell below `top` in the given XZ window is solid.
static std::vector<BlockBox> flatFloor(int top, int x0, int x1, int z0, int z1) {
    std::vector<BlockBox> out;
    for (int x = x0; x <= x1; ++x)
        for (int y = top - 2; y < top; ++y)
            for (int z = z0; z <= z1; ++z)
                out.push_back(unitBox(x, y, z));
    return out;
}

int main() {
    printf("physics primitives — client net.minecraft.world.phys port\n");

    // ================================================================
    // 1. Box primitives
    // ================================================================
    printf("\n-- AxisAlignedBB primitives --\n");
    {
        const mcp_aabb_t b = mcp_aabb_make(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);

        // expand() only moves the faces the offset pushes towards, so the
        // result covers the original box AND the translated one.
        mcp_aabb_t e = mcp_aabb_expand(b, 2.0, -3.0, 0.5);
        check(near(e.minX, 0.0) && near(e.maxX, 3.0), "expand(+x) grows only maxX");
        check(near(e.minY, -3.0) && near(e.maxY, 1.0), "expand(-y) grows only minY");
        check(near(e.minZ, 0.0) && near(e.maxZ, 1.5), "expand(+z) grows only maxZ");

        // cloneMove() translates the whole box instead.
        mcp_aabb_t t = mcp_aabb_clone_move(b, 2.0, -3.0, 0.5);
        check(near(t.minX, 2.0) && near(t.maxX, 3.0) &&
              near(t.minY, -3.0) && near(t.maxY, -2.0) &&
              near(t.minZ, 0.5) && near(t.maxZ, 1.5),
              "cloneMove translates both faces on every axis");

        mcp_aabb_t s = mcp_aabb_shrink(b, 0.1, 0.2, 0.3);
        check(near(s.minX, 0.1) && near(s.maxX, 0.9) &&
              near(s.minY, 0.2) && near(s.maxY, 0.8) &&
              near(s.minZ, 0.3) && near(s.maxZ, 0.7), "shrink insets every face");

        check(near(mcp_aabb_size(b), 1.0), "size() of the unit cube is 1");
        check(near(mcp_aabb_size(mcp_aabb_make(0, 0, 0, 0.6, 1.8, 0.6)), 1.0),
              "size() is the mean extent (0.6/1.8/0.6 player box -> 1.0)");

        // intersects() needs real interpenetration; intersectsInner()
        // counts a merely touching face.
        const mcp_aabb_t touch = mcp_aabb_make(1.0, 0.0, 0.0, 2.0, 1.0, 1.0);
        check(!mcp_aabb_intersects(b, touch), "intersects() ignores a touching face");
        check(mcp_aabb_intersects_inner(b, touch), "intersectsInner() counts a touching face");
        check(mcp_aabb_intersects(b, mcp_aabb_make(0.9, 0.9, 0.9, 1.9, 1.9, 1.9)),
              "intersects() is true when the boxes really overlap");

        // Two different containments, on purpose: mcp_aabb_contains_point()
        // is inclusive, the client's AABB::contains() is strict.
        const mcp_vector3_t corner = mcp_v3(1.0, 1.0, 1.0);
        const mcp_vector3_t inside = mcp_v3(0.5, 0.5, 0.5);
        check(mcp_aabb_contains_point(b, corner), "contains_point() accepts the face");
        check(!mcp_aabb_contains_strict(b, corner), "contains_strict() rejects the face");
        check(mcp_aabb_contains_strict(b, inside), "contains_strict() accepts an interior point");
    }

    // ================================================================
    // 2. Per-axis swept clipping
    // ================================================================
    printf("\n-- clip?Collide (swept axis clipping) --\n");
    {
        // A 1x1x1 box at the origin moving right (+x) into a wall whose
        // west face sits at x = 2: only 1.0 of the requested 5.0 is free.
        const mcp_aabb_t b   = mcp_aabb_make(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        const mcp_aabb_t wall = mcp_aabb_make(2.0, 0.0, 0.0, 3.0, 1.0, 1.0);
        check(near(mcp_aabb_clip_x_collide(b, wall, 5.0), 1.0),
              "+x move stops at the wall face (gap, not 0)");
        check(near(mcp_aabb_clip_x_collide(b, wall, 0.5), 0.5),
              "+x move shorter than the gap is left alone");
        check(near(mcp_aabb_clip_x_collide(b, wall, -5.0), -5.0),
              "moving AWAY from the wall is untouched");
        const mcp_aabb_t lwall = mcp_aabb_make(-3.0, 0.0, 0.0, -2.0, 1.0, 1.0);
        check(near(mcp_aabb_clip_x_collide(b, lwall, -5.0), -2.0),
              "-x move stops at the wall face");
        const mcp_aabb_t ceiling = mcp_aabb_make(0.0, 3.0, 0.0, 1.0, 4.0, 1.0);
        check(near(mcp_aabb_clip_y_collide(b, ceiling, 5.0), 2.0),
              "+y move stops under the ceiling");
        const mcp_aabb_t floorBox = mcp_aabb_make(0.0, -2.0, 0.0, 1.0, -1.0, 1.0);
        check(near(mcp_aabb_clip_y_collide(b, floorBox, -5.0), -1.0),
              "-y move stops on the floor");
        const mcp_aabb_t nwall = mcp_aabb_make(0.0, 0.0, -2.0, 1.0, 1.0, -1.0);
        check(near(mcp_aabb_clip_z_collide(b, nwall, -5.0), -1.0),
              "-z move stops at the wall face");

        // The obstacle's other two axes have to overlap or it is skipped.
        const mcp_aabb_t elsewhere = mcp_aabb_make(2.0, 10.0, 0.0, 3.0, 11.0, 1.0);
        check(near(mcp_aabb_clip_x_collide(b, elsewhere, 5.0), 5.0),
              "an obstacle that misses on Y/Z clips nothing");

        // Documented client behaviour: a box already flush against an
        // obstacle cannot move towards it (gap = 0), even though the move
        // "starts" at the contact plane.
        const mcp_aabb_t flush = mcp_aabb_make(1.0, 0.0, 0.0, 2.0, 1.0, 1.0);
        check(near(mcp_aabb_clip_x_collide(b, flush, 5.0), 0.0),
              "flush contact pins the move (gap 0), like the client");
    }

    // ================================================================
    // 3. sweep — Entity::move() + checkGroundState()
    // ================================================================
    printf("\n-- sweep(): Entity::move --\n");
    {
        const std::vector<BlockBox> world = flatFloor(66, 9, 13, 9, 13);

        // Free fall onto the floor: the move is clipped to land exactly on
        // top of the block at y = 66 (the box's minY), and that counts as
        // standing on the ground.
        const mcp_aabb_t item = mcp_aabb_make(10.5 - 0.125, 70.0, 10.5 - 0.125,
                                              10.5 + 0.125, 70.25, 10.5 + 0.125);
        MotionResult r = cursestone::physics::sweep(item, mcp_v3(0.0, -2.0, 0.0), world);
        check(near(r.motion.y, -2.0), "falling over open air is untouched");
        check(!r.on_ground, "an unclipped downward move is not 'on ground'");

        const mcp_aabb_t low = mcp_aabb_make(10.5 - 0.125, 66.5, 10.5 - 0.125,
                                             10.5 + 0.125, 66.75, 10.5 + 0.125);
        r = cursestone::physics::sweep(low, mcp_v3(0.0, -4.0, 0.0), world);
        check(near(r.motion.y, -0.5), "a blocked fall stops exactly on the floor");
        check(r.clipped_y && r.on_ground, "landing sets clipped_y + on_ground");
        check(!r.clipped_x && !r.clipped_z, "landing does not touch x/z");

        // Moving up into a ceiling is clipped but must NOT read as ground
        // (only a clipped DOWNWARD move is a landing).
        std::vector<BlockBox> withCeiling = world;
        withCeiling.push_back(unitBox(10, 67, 10));   // y = 67..68
        r = cursestone::physics::sweep(mcp_aabb_make(10.25, 66.0, 10.25, 10.75, 66.25, 10.75),
                                       mcp_v3(0.0, 1.0, 0.0), withCeiling);
        check(near(r.motion.y, 0.75), "a jump stops under the ceiling");
        check(r.clipped_y && !r.on_ground, "clipping an UPWARD move is not 'on ground'");

        // Walking into a wall: x is clipped, z keeps sliding (the client
        // resolves each axis on its own, which is what makes walls slide).
        std::vector<BlockBox> wallWorld = world;
        for (int y = 66; y <= 67; ++y) wallWorld.push_back(unitBox(11, y, 10));
        const mcp_aabb_t walker = mcp_aabb_make(10.25, 66.0, 10.4, 10.75, 66.25, 10.6);
        r = cursestone::physics::sweep(walker, mcp_v3(0.5, 0.0, 0.5), wallWorld);
        check(near(r.motion.x, 0.25), "the X move stops at the wall");
        check(near(r.motion.z, 0.5), "the Z move keeps sliding along it");
        check(r.clipped_x && !r.clipped_z, "only the blocked axis reports clipped");

        // Open space: nothing to hit, nothing to report.
        r = cursestone::physics::sweep(mcp_aabb_make(20.25, 66.0, 20.25, 20.75, 66.25, 20.75),
                                       mcp_v3(0.2, 0.1, -0.3), world);
        check(near(r.motion.x, 0.2) && near(r.motion.y, 0.1) && near(r.motion.z, -0.3),
              "an unobstructed move is passed through unchanged");
        check(!r.on_ground && !r.clipped_x && !r.clipped_y && !r.clipped_z,
              "an unobstructed move reports no collisions");

        // An empty box list (unloaded world) never invents a collision.
        r = cursestone::physics::sweep(item, mcp_v3(0.0, -9.0, 0.0), {});
        check(near(r.motion.y, -9.0) && !r.on_ground,
              "no collision boxes means free movement");

        // Y is resolved BEFORE X: a box that falls onto the floor and lands
        // BESIDE a wall is stopped by that wall in the same move. Resolving
        // X first (from the airborne start height, where the wall is far
        // below the box) would have let it slide straight through.
        std::vector<BlockBox> yFirst = world;
        yFirst.push_back(unitBox(11, 66, 10));   // the wall sits ON the floor
        r = cursestone::physics::sweep(mcp_aabb_make(10.25, 69.0, 10.4, 10.75, 69.25, 10.6),
                                       mcp_v3(0.5, -4.0, -1.0), yFirst);
        check(near(r.motion.y, -3.0), "the fall stops on the floor (66 - 69)");
        check(near(r.motion.x, 0.25), "X is resolved AFTER Y, so the landed box hits the wall");
        check(near(r.motion.z, -1.0), "the Z move is still free");
        check(r.clipped_y && r.clipped_x && r.on_ground, "both blocked axes are reported");
    }

    // ================================================================
    // 4. clipRay — AABB::clip + HitResult
    // ================================================================
    printf("\n-- clipRay(): AABB::clip --\n");
    {
        const std::vector<BlockBox> world = flatFloor(66, 9, 13, 9, 13);

        // Straight down onto the block at (10, 65, 9): the top face is
        // y = 66 (MCP_FACE_UP), and the hit point is the entry point.
        mcp_hit_result_t h = cursestone::physics::clipRay(mcp_v3(10.5, 70.0, 10.5),
                                                         mcp_v3(10.5, 60.0, 10.5), world);
        check(mcp_hit_is_hit(&h), "a ray into the floor hits something");
        check(h.type == MCP_HIT_TILE, "the hit is a TILE hit");
        check(h.x == 10 && h.y == 65 && h.z == 10, "the hit carries the block coordinates");
        check(h.face == MCP_FACE_UP, "the floor is entered through its UP face");
        check(near(h.pos.x, 10.5) && near(h.pos.y, 66.0) && near(h.pos.z, 10.5),
              "the hit point is exactly on the block's top face");

        // Sideways into the same column, entering through the west face.
        // (A dedicated single block: a ray threaded exactly between two
        // side-by-side blocks is a genuine tie, and the tie-break is list
        // order, so it says nothing about the clip itself.)
        const std::vector<BlockBox> one = { unitBox(10, 65, 10) };
        h = cursestone::physics::clipRay(mcp_v3(5.0, 65.5, 10.5),
                                         mcp_v3(15.0, 65.5, 10.5), one);
        check(mcp_hit_is_hit(&h) && h.x == 10 && h.y == 65 && h.z == 10,
              "a horizontal ray finds the block");
        check(h.face == MCP_FACE_WEST,
              "entering from -x is the WEST face (4), matching the protocol");
        check(near(h.pos.x, 10.0) && near(h.pos.y, 65.5),
              "the hit point is on the struck face");

        // Two blocks in the path: the NEAREST one wins.
        std::vector<BlockBox> pair;
        pair.push_back(unitBox(12, 65, 10));
        pair.push_back(unitBox(10, 65, 10));
        h = cursestone::physics::clipRay(mcp_v3(9.5, 65.5, 10.5),
                                         mcp_v3(15.0, 65.5, 10.5), pair);
        check(mcp_hit_is_hit(&h) && h.x == 10,
              "the nearest box in the path wins (regardless of list order)");

        // A ray that misses everything.
        h = cursestone::physics::clipRay(mcp_v3(10.5, 90.0, 10.5),
                                         mcp_v3(10.5, 80.0, 10.5), world);
        check(!mcp_hit_is_hit(&h), "a ray through open sky reports no hit");
        check(h.face == -1 && h.type == MCP_HIT_NONE, "no hit reports NO_HIT / face -1");

        // A segment that ends short of the block is not a hit (this is a
        // SEGMENT trace, not an infinite ray).
        h = cursestone::physics::clipRay(mcp_v3(10.5, 70.0, 10.5),
                                         mcp_v3(10.5, 67.0, 10.5), world);
        check(!mcp_hit_is_hit(&h), "a segment that stops short does not hit");
    }

    // ================================================================
    // 5. Vec3 clip helpers
    // ================================================================
    printf("\n-- Vec3 clip helpers --\n");
    {
        mcp_vector3_t out;
        const mcp_vector3_t a = mcp_v3(0.0, 0.0, 0.0), b = mcp_v3(2.0, 4.0, 6.0);
        check(mcp_v3_clip_x(a, b, 1.0, &out) &&
              near(out.x, 1.0) && near(out.y, 2.0) && near(out.z, 3.0),
              "clipX interpolates the crossing point");
        check(!mcp_v3_clip_x(a, b, 5.0, &out), "clipX rejects a plane beyond the segment");
        check(!mcp_v3_clip_x(a, mcp_v3(0.0, 1.0, 1.0), 1.0, &out),
              "clipX rejects a parallel segment");
        check(near(mcp_v3_dist_sq(mcp_v3(1, 2, 3), mcp_v3(4, 6, 3)), 25.0),
              "dist_sq is the square of the distance");
        const mcp_vector3_t mid = mcp_v3_lerp(a, b, 0.5);
        check(near(mid.x, 1.0) && near(mid.y, 2.0) && near(mid.z, 3.0),
              "lerp(0.5) lands halfway");
    }

    // ================================================================
    // 6. Equivalence with the inline integrator this replaced
    // ================================================================
    // World::tickDroppedItems() used to carry its own copy of the three
    // clip loops. The port is only a refactor if sweep() answers exactly
    // the same thing, so run the old algorithm side by side with it over
    // a few thousand random worlds and moves in which the item is dropped
    // into a genuinely solid/empty grid.
    printf("\n-- equivalence with the inline item integrator --\n");
    {
        // Deterministic LCG — a fixed seed means a failure here is always
        // reproducible.
        uint64_t seed = 0x9E3779B97F4A7C15ULL;
        auto rnd = [&seed]() -> uint64_t {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            return seed >> 17;
        };
        auto rndf = [&rnd](double lo, double hi) {
            return lo + (double)(rnd() % 1000000ULL) / 1000000.0 * (hi - lo);
        };

        int mismatches = 0;
        int landings = 0;
        std::vector<BlockBox> grid;

        for (int iter = 0; iter < 4000; ++iter) {
            // --- one random world: a solid floor slab over a 7x7 column
            //     range plus random pillars standing on it. Only full
            //     cubes, because that is what the old inline loops assumed
            //     (they had no access to a block's own box).
            grid.clear();
            const int gy0 = 60 + (int)(rnd() % 4);   // the floor level
            for (int x = -3; x <= 3; ++x)
                for (int z = -3; z <= 3; ++z) {
                    grid.push_back(unitBox(x, gy0, z));
                    for (int y = gy0 + 1; y <= gy0 + 4; ++y)
                        if ((rnd() % 4) == 0) grid.push_back(unitBox(x, y, z));
                }

            // --- one random move of a dropped-item-sized box, spawned
            //     over the floor so that landings are common (the guard
            //     rail below counts them).
            const double half = 0.125;
            double ix = rndf(-2.5, 2.5), iz = rndf(-2.5, 2.5);
            double iy = (double)gy0 + 1.0 + rndf(0.0, 3.0);
            mcp_vector3_t m = mcp_v3(rndf(-0.4, 0.4), rndf(-0.6, 0.2), rndf(-0.4, 0.4));
            mcp_aabb_t box = mcp_aabb_make(ix - half, iy, iz - half,
                                           ix + half, iy + 0.25, iz + half);

            // lookup region exactly as the inline code computed it
            mcp_aabb_t sw = mcp_aabb_clone_move(box, m.x, m.y, m.z);
            int x0 = (int)std::floor(sw.minX), x1 = (int)std::ceil(sw.maxX);
            int y0 = (int)std::floor(sw.minY), y1 = (int)std::ceil(sw.maxY);
            int z0 = (int)std::floor(sw.minZ), z1 = (int)std::ceil(sw.maxZ);

            // the boxes the world would have handed over for that region
            std::vector<BlockBox> local;
            for (const BlockBox& b : grid)
                if (b.x >= x0 && b.x <= x1 && b.y >= y0 && b.y <= y1 &&
                    b.z >= z0 && b.z <= z1) local.push_back(b);

            // --- reference: the three clip loops, verbatim
            double dx = m.x, dy = m.y, dz = m.z;
            mcp_aabb_t bb = box;
            for (const BlockBox& c : local) {
                if (bb.maxX <= c.box.minX || bb.minX >= c.box.maxX) continue;
                if (bb.maxZ <= c.box.minZ || bb.minZ >= c.box.maxZ) continue;
                if (dy > 0 && bb.maxY <= c.box.minY) {
                    double t = c.box.minY - bb.maxY;
                    if (t < dy) dy = t;
                }
                if (dy < 0 && bb.minY >= c.box.maxY) {
                    double t = c.box.maxY - bb.minY;
                    if (t > dy) dy = t;
                }
            }
            bb.minY += dy; bb.maxY += dy;
            for (const BlockBox& c : local) {
                if (bb.maxY <= c.box.minY || bb.minY >= c.box.maxY) continue;
                if (bb.maxZ <= c.box.minZ || bb.minZ >= c.box.maxZ) continue;
                if (dx > 0 && bb.maxX <= c.box.minX) {
                    double t = c.box.minX - bb.maxX;
                    if (t < dx) dx = t;
                }
                if (dx < 0 && bb.minX >= c.box.maxX) {
                    double t = c.box.maxX - bb.minX;
                    if (t > dx) dx = t;
                }
            }
            bb.minX += dx; bb.maxX += dx;
            for (const BlockBox& c : local) {
                if (bb.maxX <= c.box.minX || bb.minX >= c.box.maxX) continue;
                if (bb.maxY <= c.box.minY || bb.minY >= c.box.maxY) continue;
                if (dz > 0 && bb.maxZ <= c.box.minZ) {
                    double t = c.box.minZ - bb.maxZ;
                    if (t < dz) dz = t;
                }
                if (dz < 0 && bb.minZ >= c.box.maxZ) {
                    double t = c.box.maxZ - bb.minZ;
                    if (t > dz) dz = t;
                }
            }
            const bool old_ground = (m.y != dy && m.y < 0.0);
            if (old_ground) ++landings;

            // --- the ported engine
            MotionResult r = cursestone::physics::sweep(box, m, local);

            if (!near(r.motion.x, dx) || !near(r.motion.y, dy) || !near(r.motion.z, dz) ||
                 r.on_ground != old_ground ||
                 r.clipped_x != (dx != m.x) || r.clipped_y != (dy != m.y) ||
                 r.clipped_z != (dz != m.z)) {
                if (mismatches == 0) {
                    printf("  first mismatch at iter %d: old=(%.6f,%.6f,%.6f) new=(%.6f,%.6f,%.6f)\n",
                           iter, dx, dy, dz, r.motion.x, r.motion.y, r.motion.z);
                }
                ++mismatches;
            }
        }

        check(mismatches == 0,
              "sweep() matches the inline integrator on 4000 random worlds");
        // Guard rail on the test itself: if the random worlds never
        // produced a landing, the comparison above proved nothing.
        check(landings > 50,
              "the random scenarios actually produced landings (" +
                  std::to_string(landings) + ")");
    }

    printf("\n%s (%d failure(s))\n",
           failures ? "PHYS TEST: FAIL" : "PHYS TEST: OK", failures);
    return failures ? 1 : 0;
}
