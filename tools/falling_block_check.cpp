// tools/falling_block_check.cpp
//
// Falling-block (protocol 84 / Infinity FallingSand) regression check.
//
// Covers the two pieces of the falling-block system that can be verified
// standalone, against the Infinity (MCPE 0.15.10) reference:
//
//   1. AddEntityPacket wire format with entity metadata — the falling
//      block is spawned as entity type 66 carrying the DATA_BLOCK_INFO
//      (20) int `blockId | (damage << 8)`. Infinity Binary::writeMetadata
//      encodes INT values LITTLE-endian (writeLInt) with the 0x7F
//      terminator; empty metadata keeps the historical single 0x00 byte
//      this server always sent (arrows/lightning).
//
//   2. The physics numbers the World::tickFallingBlocks() integrator
//      relies on: a 0.98-wide falling box under gravity 0.04 / drag 0.98
//      clips EXACTLY at the block top (contact position is an integer),
//      the landing cell floor(y + 0.5) is the cell ABOVE the floor block
//      (never the floor block itself), and a stacked column lands as a
//      column (the upper entity rests on the block the lower one just
//      became instead of tunneling into its cell).
//
//   g++ -std=c++20 -Isrc tools/falling_block_check.cpp \
//       src/network/protocol/AddEntityPacket.cpp -o /tmp/falling_block_check
//   /tmp/falling_block_check
//
#include "network/protocol/AddEntityPacket.h"
#include "physics/Physics.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace cursestone;
using namespace cursestone::network::protocol;

static int g_failures = 0;
#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (cond) {                                               \
            std::printf("  OK   %s\n", msg);                      \
        } else {                                                  \
            std::printf("  FAIL %s\n", msg);                      \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

// ---------------------------------------------------------------
// Infinity Binary::writeMetadata (protocol 84), reference encoding of
// the DATA_BLOCK_INFO entry for sand meta 1 (red sand):
//   key byte  (DATA_TYPE_INT(2) << 5) | 20 = 0x54
//   value     little-endian int32 of 12 | (1 << 8) = 0x010C
//   terminator 0x7F
// ---------------------------------------------------------------
static void test_metadata_wire() {
    std::printf("[1] AddEntityPacket metadata wire format\n");

    AddEntityPacket pk;
    pk.eid = 0x1122334455667788LL;
    pk.type = AddEntityPacket::TYPE_FALLING_BLOCK;   // 66 (Infinity FallingSand)
    pk.x = 100.5f; pk.y = 64.0f; pk.z = -33.5f;
    SetEntityDataPacket::Entry meta;
    meta.index = SetEntityDataPacket::PROP_BLOCK_INFO;  // 20
    meta.type  = SetEntityDataPacket::TYPE_INT;         // 2
    meta.ival  = (int32_t)((uint32_t)12 | ((uint32_t)1 << 8));  // red sand
    pk.metadata.push_back(meta);

    std::vector<uint8_t> out;
    pk.encode(out);

    bool ok = true;
    size_t i = 0;
    auto expect = [&](uint8_t b, const char* what) {
        if (i >= out.size() || out[i] != b) {
            std::printf("  (byte %zu: expected 0x%02X, got 0x%02X — %s)\n",
                        i, b, i < out.size() ? out[i] : 0, what);
            ok = false;
        }
        ++i;
    };
    expect(0x11, "eid BE byte 0");          // int64 BE 0x1122334455667788
    expect(0x22, "eid BE byte 1");
    expect(0x33, "eid BE byte 2");
    expect(0x44, "eid BE byte 3");
    expect(0x55, "eid BE byte 4");
    expect(0x66, "eid BE byte 5");
    expect(0x77, "eid BE byte 6");
    expect(0x88, "eid BE byte 7");
    expect(0x00, "type BE byte 0");         // int32 BE 66 = 0x00000042
    expect(0x00, "type BE byte 1");
    expect(0x00, "type BE byte 2");
    expect(0x42, "type BE byte 3");
    i += 6 * 4;                             // x,y,z + speedX/Y/Z floats
    i += 2 * 4;                             // yaw, pitch (0 scaled)
    i += 4;                                 // modifiers
    expect(0x54, "meta key (INT<<5)|20");
    expect(0x0C, "DATA_BLOCK_INFO LE byte 0 (12)");
    expect(0x01, "DATA_BLOCK_INFO LE byte 1 (damage << 8)");
    expect(0x00, "DATA_BLOCK_INFO LE byte 2");
    expect(0x00, "DATA_BLOCK_INFO LE byte 3");
    expect(0x7F, "metadata terminator");
    expect(0x00, "links count hi");
    expect(0x00, "links count lo");
    CHECK(ok && out.size() == i, "metadata wire bytes match Infinity writeMetadata");

    // Empty metadata keeps the legacy single 0x00 byte.
    AddEntityPacket bare;
    bare.eid = 1;
    bare.type = AddEntityPacket::TYPE_ARROW;
    std::vector<uint8_t> out2;
    bare.encode(out2);
    CHECK(out2.size() == 8 + 4 + 6 * 4 + 2 * 4 + 4 + 1 + 2,
          "empty metadata body length unchanged");
    CHECK(out2[8 + 4 + 6 * 4 + 2 * 4 + 4] == 0x00,
          "empty metadata stays single 0x00 (legacy arrows/lightning path)");
}

// ---------------------------------------------------------------
// Physics: the falling box under the Infinity FallingSand constants.
// gravity 0.04 per tick, drag 0.98 after the move (server integrator).
// ---------------------------------------------------------------
static void test_fall_physics() {
    std::printf("[2] falling-box physics (gravity 0.04, box 0.98)\n");

    // Floor: one solid block at cell (0, 63, 0) → top at y = 64.
    std::vector<physics::BlockBox> boxes;
    physics::BlockBox floor;
    floor.x = 0; floor.y = 63; floor.z = 0;
    floor.box = mcp_aabb_make(0, 63, 0, 1, 64, 1);
    boxes.push_back(floor);

    // Entity spawned at the centre of cell (0, 70, 0).
    double x = 0.5, y = 70.0, z = 0.5;
    double vy = 0.0;
    bool landed = false;
    double land_y = 0.0;
    for (int t = 0; t < 200 && !landed; ++t) {
        vy -= 0.04;
        mcp_aabb_t bb = mcp_aabb_make(x - 0.49, y, z - 0.49,
                                      x + 0.49, y + 0.98, z + 0.49);
        mcp_aabb_t lookup = mcp_aabb_clone_move(bb, 0.0, vy, 0.0);
        physics::MotionResult res = physics::sweep(bb, mcp_v3(0.0, vy, 0.0), boxes);
        (void)lookup;
        y += res.motion.y;
        vy *= 0.98;   // drag (Infinity: motionY *= 1 - drag)
        if (res.on_ground) { landed = true; land_y = y; }
    }

    CHECK(landed, "falling box lands on the floor block");
    // Contact is EXACT: the sweep clips to the block top (integer 64).
    CHECK(std::fabs(land_y - 64.0) < 1e-9,
          "contact lands exactly at the block top (y == 64.0)");
    // Landing cell = floor(y + 0.5) → 64, the cell ABOVE the floor block
    // (63) — never the floor block itself.
    CHECK((int)std::floor(land_y + 0.5) == 64,
          "landing cell floor(y+0.5) is the cell above the floor block");

    // The tick after contact: a new tiny downward move clips instantly
    // (gap 0) — no tunneling, no oscillation.
    vy -= 0.04;
    mcp_aabb_t bb2 = mcp_aabb_make(x - 0.49, land_y, z - 0.49,
                                   x + 0.49, land_y + 0.98, z + 0.49);
    physics::MotionResult res2 = physics::sweep(bb2, mcp_v3(0.0, vy, 0.0), boxes);
    CHECK(res2.on_ground && std::fabs(res2.motion.y) < 1e-12,
          "tick after contact stays on ground with zero motion");
}

// ---------------------------------------------------------------
// Stacked column: two entities one cell apart, integrated bottom-up.
// The lower one lands and "becomes" a block; the upper one must then
// clip against that block and land in the cell ABOVE it — not in the
// same cell (which would turn the column into item drops).
// ---------------------------------------------------------------
static void test_stacked_column() {
    std::printf("[3] stacked column lands as a column\n");

    std::vector<physics::BlockBox> boxes;
    physics::BlockBox ground;
    ground.x = 0; ground.y = 49; ground.z = 0;
    ground.box = mcp_aabb_make(0, 49, 0, 1, 50, 1);
    boxes.push_back(ground);

    double ax = 0.5, ay = 52.0, az = 0.5;   // lower entity (cell 52)
    double bx = 0.5, by = 53.0, bz = 0.5;   // upper entity (cell 53)
    double avy = 0.0, bvy = 0.0;
    bool a_landed = false, b_landed = false;
    int a_cell = 0, b_cell = 0;

    for (int t = 0; t < 200 && !(a_landed && b_landed); ++t) {
        // bottom-up, exactly like World::tickFallingBlocks' sorted sweep
        if (!a_landed) {
            avy -= 0.04;
            mcp_aabb_t bb = mcp_aabb_make(ax - 0.49, ay, az - 0.49,
                                          ax + 0.49, ay + 0.98, az + 0.49);
            physics::MotionResult res = physics::sweep(bb, mcp_v3(0.0, avy, 0.0), boxes);
            ay += res.motion.y;
            avy *= 0.98;
            if (res.on_ground) {
                a_landed = true;
                a_cell = (int)std::floor(ay + 0.5);
                // lower entity converts into a real block at its cell
                physics::BlockBox ab;
                ab.x = 0; ab.y = a_cell; ab.z = 0;
                ab.box = mcp_aabb_make(0, a_cell, 0, 1, a_cell + 1, 1);
                boxes.push_back(ab);
            }
        }
        if (!b_landed) {
            bvy -= 0.04;
            mcp_aabb_t bb = mcp_aabb_make(bx - 0.49, by, bz - 0.49,
                                          bx + 0.49, by + 0.98, bz + 0.49);
            physics::MotionResult res = physics::sweep(bb, mcp_v3(0.0, bvy, 0.0), boxes);
            by += res.motion.y;
            bvy *= 0.98;
            if (res.on_ground) {
                b_landed = true;
                b_cell = (int)std::floor(by + 0.5);
            }
        }
    }

    CHECK(a_landed && b_landed, "both column entities land");
    CHECK(a_cell == 50, "lower entity lands in the emptied cell (50)");
    CHECK(b_cell == a_cell + 1, "upper entity lands stacked on the lower one");
    CHECK(b_cell != a_cell, "no two entities claim the same landing cell");
}

int main() {
    std::printf("CurseStone falling-block check (protocol 84 / Infinity FallingSand)\n");
    test_metadata_wire();
    test_fall_physics();
    test_stacked_column();
    if (g_failures == 0) {
        std::printf("ALL CHECKS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
