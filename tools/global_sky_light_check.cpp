// tools/global_sky_light_check.cpp
//
// Unit check for the GLOBAL sky light policy (Aquamarine
// populateSkyLight semantics: every block reads sky light 15, caves and
// oceans included) plus the block-light pass that still has to behave.
//
//   1. mcp_chunk_propagate_sky_light() fills sky light 15 EVERYWHERE —
//      under a solid stone roof, on the deep floor, above the roof.
//      No column decay, no flood, no shadows left behind.
//   2. Block light still propagates for real: a glowstone seeds 15,
//      neighbours step down by 1, out-of-range cells read 0.
//   3. mcp_chunk_fill_sky_light() heals dark arrays (the disk-load
//      path) without marking the chunk dirty, and marks light_populated.
//   4. mcp_chunk_network_serialize() ships the global sky on the wire:
//      the whole 16384-byte sky section is 0xFF, while the block-light
//      section carries the real glowstone values.
//
// Build (the chunk layer plus local Block::lightOpacity/lightEmission
// tables, so Block.cpp's registry stays out of the link):
//   g++ -std=c++20 -Isrc tools/global_sky_light_check.cpp
//       src/world/format/Chunk.cpp src/world/format/ChunkSection.cpp
//       -o /tmp/global_sky_light_check
#include "block/Block.h"
#include "world/format/Chunk.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---- local light tables (the real ones live in Block.cpp; the check
// only needs a stable set: air, stone, glowstone, torch) ----
static uint8_t opac[256];
static uint8_t emis[256];

namespace cursestone::block {
uint8_t Block::lightOpacity(uint8_t block_id) {
    return opac[block_id];
}
uint8_t Block::lightEmission(uint8_t block_id) {
    return emis[block_id];
}
} // namespace cursestone::block

static void initTables() {
    opac[0] = 0;   // air
    opac[1] = 15;  // stone: fully opaque
    emis[89] = 15; // glowstone
    emis[50] = 14; // torch
}

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* what) {
    ++checks;
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static uint8_t skyAt(mcp_chunk_t* c, int x, int y, int z) {
    return mcp_chunk_get_sky_light(c, x, y, z);
}
static uint8_t blkAt(mcp_chunk_t* c, int x, int y, int z) {
    return mcp_chunk_get_block_light(c, x, y, z);
}

int main() {
    initTables();

    // ---- chunk 1: stone floor + solid roof + two emitters inside ----
    mcp_chunk_t* c = mcp_chunk_new(0, 0);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y <= 3; ++y)
                mcp_chunk_set_block_id(c, x, y, z, 1);
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            mcp_chunk_set_block_id(c, x, 40, z, 1); // solid roof
    mcp_chunk_set_block_id(c, 5, 20, 5, 89);        // glowstone
    mcp_chunk_set_block_id(c, 10, 20, 10, 50);      // torch
    mcp_chunk_recalculate_height_map(c);
    mcp_chunk_propagate_sky_light(c);

    printf("stage: global sky light fills the whole chunk\n");
    check(skyAt(c, 8, 30, 8) == 15, "sky=15 under the solid stone roof (y=30)");
    check(skyAt(c, 8, 10, 8) == 15, "sky=15 deep in the sealed room (y=10)");
    check(skyAt(c, 8, 4, 8) == 15, "sky=15 just above the stone floor (y=4)");
    check(skyAt(c, 8, 39, 8) == 15, "sky=15 right under the roof slab (y=39)");
    check(skyAt(c, 8, 41, 8) == 15, "sky=15 above the roof (y=41)");
    check(skyAt(c, 0, 20, 15) == 15, "sky=15 at the room's corner");
    check(skyAt(c, 15, 127, 15) == 15, "sky=15 at world top (unallocated section)");

    printf("stage: block light still propagates for real\n");
    check(blkAt(c, 5, 20, 5) == 15, "glowstone cell reads its own emission 15");
    check(blkAt(c, 6, 20, 5) == 14, "one step from the glowstone = 14");
    check(blkAt(c, 7, 20, 5) == 13, "two steps from the glowstone = 13");
    check(blkAt(c, 10, 20, 10) == 14, "torch cell reads its own emission 14");
    check(blkAt(c, 0, 20, 15) == 0,
          "cell 15+ steps from every emitter reads 0");
    check(c->light_populated, "propagate marks light_populated");
    check(c->dirty, "propagate marks the chunk dirty");

    // ---- chunk 2: fill_sky_light heals without dirtying ----
    mcp_chunk_t* c2 = mcp_chunk_new(1, 0);
    mcp_chunk_set_block_id(c2, 8, 5, 8, 1);
    mcp_chunk_recalculate_height_map(c2);
    mcp_chunk_propagate_sky_light(c2);
    c2->dirty = false;
    // simulate a disk image written before the global-light switch
    mcp_chunk_set_sky_light(c2, 3, 2, 3, 0);
    check(skyAt(c2, 3, 2, 3) == 0, "simulated old-disk shadow reads 0 first");
    c2->dirty = false; // the poke accessor marks dirty; clear it again
    mcp_chunk_fill_sky_light(c2);
    check(skyAt(c2, 3, 2, 3) == 15, "fill_sky_light heals the dark cell to 15");
    check(!c2->dirty, "fill_sky_light does not mark the chunk dirty");
    check(c2->light_populated, "fill_sky_light marks light_populated");

    // ---- wire shape: the serialized chunk carries global sky ----
    size_t len = 0;
    uint8_t* buf = mcp_chunk_network_serialize(c, &len);
    check(buf != nullptr && len == 83204,
          "network serialize produces the full 83204-byte column payload");
    if (buf) {
        bool skyAllFF = true;
        for (size_t i = 49152; i < 65536; ++i)
            if (buf[i] != 0xFF) {
                skyAllFF = false;
                break;
            }
        check(skyAllFF, "the whole 16384-byte sky section on the wire is 0xFF");
        // glowstone at (x=5,z=5,y=20): ci = 5<<11 | 5<<7 | 20; even y →
        // low nibble = 15, high nibble = the y=21 cell (14 through air).
        uint32_t ci = (5u << 11) | (5u << 7) | 20u;
        check(buf[65536 + (ci >> 1)] == 0xEF,
              "block-light section carries the real glowstone gradient");
        free(buf);
    }

    mcp_chunk_destroy(c);
    mcp_chunk_destroy(c2);

    printf("\n%s (%d checks, %d failures)\n",
           failures == 0 ? "ALL PASSED" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}
