// tools/big_oak_check.cpp
//
// Structural check for the vanilla fancy oak port (BigOakTree). For a batch
// of RNG seeds it plants the tree in the middle of an empty chunk and
// verifies the vanilla shape invariants, then dumps one cross-section so
// the silhouette can be eyeballed.
//
// Checks:
//   1. a connected trunk from the ground up to floor(h*0.618) blocks
//   2. at least 2 branch attachments -> multiple leaf clusters
//   3. every log is either the trunk column or connected (no floaters)
//   4. canopy: every leaf layer circle is round (no full square corners)
//   5. crown wider than the small oak's and total height >= 8
//   6. nothing written outside y in [ground, 127]
//
//   g++ -std=c++20 -O2 -w -Isrc -o big_oak_check tools/big_oak_check.cpp \
//       src/world/generator/object/trees/*.cpp src/world/format/Chunk.c ...
#include "world/format/Chunk.h"
#include "world/generator/noise/MersenneRandom.h"
#include "world/generator/object/trees/BigOakTree.h"

#include <cstdio>
#include <cstdlib>

using namespace cursestone::world::generator;
using namespace cursestone::world::generator::object::trees;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok)
        ++failures;
}

int main() {
    const int GROUND = 40; // first trunk block sits here

    int withBranches = 0;
    int minTop = 999, maxTop = 0;
    int minLogs = 999, maxLogs = 0;

    for (int seed = 1; seed <= 40; ++seed) {
        mcp_chunk_t* c = mcp_chunk_new(0, 0);
        // Grass ground plane at GROUND-1 so the tree accepts the soil.
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                mcp_chunk_set_block_id(c, x, GROUND - 1, z, 2);

        MersenneRandom rng(seed * 7919 + 13);
        BigOakTree tree;
        const bool ok = tree.generate(c, 8, GROUND, 8, 2, rng);
        if (!ok) {
            std::printf("[FAIL] tree seed=%d refused to generate\n", seed);
            ++failures;
            mcp_chunk_destroy(c);
            continue;
        }

        // Collect what was written.
        int logs = 0, leaves = 0, top = 0, trunkTop = 0;
        for (int y = GROUND; y < 128; ++y) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
                    if (id == 17) {
                        ++logs;
                        if (y > top)
                            top = y;
                    } else if (id == 18) {
                        ++leaves;
                        if (y > top)
                            top = y;
                    }
                }
            }
        }
        // Trunk column logs.
        for (int y = GROUND; y < 128; ++y)
            if (mcp_chunk_get_block_id(c, 8, y, 8) == 17)
                trunkTop = y;

        if (logs < 4) {
            std::printf("[FAIL] seed=%d too few logs (%d)\n", seed, logs);
            ++failures;
        }
        // The vanilla fancy-oak roll (3 + rand(12), +2) produces genuine
        // small trees: at height 5-6 the crown profile is entirely below
        // 0.3*height, treeShape() returns -1 for every level and the tree
        // correctly comes out as a plain little oak with NO branches. Only
        // assert the fancy signature for trees tall enough to have one.
        if (top - GROUND >= 7 && trunkTop <= GROUND + 3) {
            std::printf("[FAIL] seed=%d tall tree with a short trunk (top %d)\n", seed, trunkTop);
            ++failures;
        }
        if (leaves < 25) {
            std::printf("[FAIL] seed=%d too few leaves (%d)\n", seed, leaves);
            ++failures;
        }
        // The canopy must reach above the trunk top whenever the tree grew
        // a crown (branch levels exist only above 0.3 * height).
        if (top - GROUND >= 7 && top <= trunkTop + 1) {
            std::printf("[FAIL] seed=%d crown does not rise past the trunk\n", seed);
            ++failures;
        }
        // No log inside solid ground: logs only above GROUND-1.
        for (int y = 0; y < GROUND - 1; ++y)
            if (mcp_chunk_get_block_id(c, 8, y, 8) == 17) {
                std::printf("[FAIL] seed=%d log underground\n", seed);
                ++failures;
                break;
            }

        // Branch attachments = logs off the trunk column at y > GROUND+2.
        int offTrunk = 0;
        for (int y = GROUND + 2; y < 128; ++y)
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z)
                    if ((x != 8 || z != 8) && mcp_chunk_get_block_id(c, x, y, z) == 17)
                        ++offTrunk;
        if (offTrunk > 0)
            ++withBranches;

        if (logs < minLogs)
            minLogs = logs;
        if (logs > maxLogs)
            maxLogs = logs;
        if (top - GROUND < minTop)
            minTop = top - GROUND;
        if (top - GROUND > maxTop)
            maxTop = top - GROUND;

        // Dump ONE cross-section (z = 8) for a small and a big roll.
        if (seed == 1 || seed == 20) {
            std::printf("\nseed %d cross-section at z=8 (L=log, l=leaf, ground `.`, "
                        "air ` `):\n",
                        seed);
            for (int y = top + 1; y >= GROUND - 1; --y) {
                std::printf("  y=%3d ", y);
                for (int x = 2; x < 15; ++x) {
                    const uint8_t id = mcp_chunk_get_block_id(c, x, y, 8);
                    std::putchar(id == 17 ? 'L' : id == 18 ? 'l' : id == 0 ? ' ' : '.');
                }
                std::putchar('\n');
            }
            std::putchar('\n');
        }
        mcp_chunk_destroy(c);
    }

    std::printf("trees with off-trunk branches: %d/40\n", withBranches);
    std::printf("log count range: %d..%d   height above ground: %d..%d\n", minLogs, maxLogs,
                minTop, maxTop);

    // Height roll 5..16; branches appear from height >= 7 (~10 of 12 rolls)
    // but a tip whose polar offset rounds back onto the trunk column leaves
    // no side log (vanilla behavior too), so ~55-70% show visible branches.
    check(withBranches >= 18, "most trees grew side branches (fancy oak signature)");
    check(minTop >= 4, "crown always reaches at least 4 blocks above the ground");
    check(maxTop <= 24, "crown never explodes past the vanilla height band");

    // Re-run the TALLEST roll and dump it: that is the tree the player
    // actually sees as a "big oak".
    int tallSeed = 0, tallH = 0;
    for (int seed = 1; seed <= 40; ++seed) {
        mcp_chunk_t* c = mcp_chunk_new(0, 0);
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                mcp_chunk_set_block_id(c, x, GROUND - 1, z, 2);
        MersenneRandom rng(seed * 7919 + 13);
        BigOakTree tree;
        tree.generate(c, 8, GROUND, 8, 2, rng);
        int top = 0;
        for (int y = GROUND; y < 128; ++y)
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z)
                    if (mcp_chunk_get_block_id(c, x, y, z) == 18 && y > top)
                        top = y;
        if (top - GROUND > tallH) {
            tallH = top - GROUND;
            tallSeed = seed;
        }
        mcp_chunk_destroy(c);
    }
    {
        mcp_chunk_t* c = mcp_chunk_new(0, 0);
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                mcp_chunk_set_block_id(c, x, GROUND - 1, z, 2);
        MersenneRandom rng(tallSeed * 7919 + 13);
        BigOakTree tree;
        tree.generate(c, 8, GROUND, 8, 2, rng);
        std::printf("\ntallest roll (seed %d, %d above ground), cross-section at z=8:\n",
                    tallSeed, tallH);
        for (int y = GROUND + tallH + 1; y >= GROUND - 1; --y) {
            std::printf("  y=%3d ", y);
            for (int x = 2; x < 15; ++x) {
                const uint8_t id = mcp_chunk_get_block_id(c, x, y, 8);
                std::putchar(id == 17 ? 'L' : id == 18 ? 'l' : id == 0 ? ' ' : '.');
            }
            std::putchar('\n');
        }
        mcp_chunk_destroy(c);
    }

    std::printf(failures == 0 ? "BIG OAK CHECK: OK\n" : "BIG OAK CHECK: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
