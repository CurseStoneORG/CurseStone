// tools/light_check.cpp
//
// Regression test for the lighting bugs reported by the player:
//
//   1. "La iluminacion es rara, no se distribuye bien" / "dentro de
//      cuevas o partes tapadas por algo como una parte superior siempre
//      quedan manchas negras".
//      The old pass (mcp_chunk_propagate_sky_light) flooded only the four
//      HORIZONTAL neighbours. Light could not step down, so anything under
//      a roof stayed at 0 no matter how wide the opening was.
//
//   2. "La luz que se distribuya bien y vanilla, por todos lados".
//      Block light was never computed at all: mcp_block_light_emission()
//      was dead code, so torches and glowstone lit nothing and a torch-lit
//      cave stayed black.
//
//   3. "Que se actualice la luz bien".
//      Level::setBlock() wrote the block and stopped. Building a roof left
//      the room at full daylight; breaking it left the room in the dark.
//
//   4. Chunk borders: light was computed per chunk in isolation, so the
//      seam between two chunks was always dark.
//
//   g++ -std=c++20 -Isrc tools/light_check.cpp
//       <every object except main.o> -lz -pthread -o light_check
#include "Server.h"
#include "block/BlockIds.h"
#include "block/BlockRegistry.h"
#include "block/BlockLightC.h"
#include "level/ChunkManager.h"
#include "level/Level.h"
#include "level/format/Chunk.h"

#include <cstdio>
#include <string>

using namespace cursestone;
namespace blk = cursestone::block;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& what) {
    ++checks;
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static const uint8_t AIR = 0;
static const uint8_t STONE = (uint8_t)blk::BlockId::STONE;

// Reads need the chunk resident; the callers below always load first.
static uint8_t skyAt(level::Level& lvl, int x, int y, int z) {
    mcp_chunk_t* c = lvl.chunks().getChunkIfLoaded(x >> 4, z >> 4);
    if (!c) return 0;
    return mcp_chunk_get_sky_light(c, x & 15, y, z & 15);
}

static uint8_t blockLightAt(level::Level& lvl, int x, int y, int z) {
    mcp_chunk_t* c = lvl.chunks().getChunkIfLoaded(x >> 4, z >> 4);
    if (!c) return 0;
    return mcp_chunk_get_block_light(c, x & 15, y, z & 15);
}

// Every block goes through Level::setBlock(), which is the path the game
// uses and the one that has to update the lighting.
static void set(level::Level& lvl, int x, int y, int z, uint8_t id,
                uint8_t data = 0) {
    lvl.setBlock(x, y, z, id, data);
}

static void fill(level::Level& lvl, int x0, int y0, int z0, int x1, int y1,
                 int z1, uint8_t id) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) set(lvl, x, y, z, id);
}

// Let the queued chunk relights drain.
static void drain(level::Level& lvl, int ticks = 8) {
    for (int i = 0; i < ticks; ++i) lvl.tick((uint64_t)i);
}

static level::LevelConfig makeConfig(const char* name, int64_t seed) {
    level::LevelConfig cfg;
    cfg.name = name;
    cfg.dataPath = std::string("/tmp/csb/") + name;
    cfg.seed = seed;
    return cfg;
}

// Builds a room inside solid rock: floor at floorY, open interior up to
// ceilY, and a roof row at ceilY (stone when roofed, open sky when not).
//
// The stone mass stops AT ceilY. Carving the roof row afterwards is what
// leaves the room genuinely open to the sky in the `!roofed` case —
// filling one block higher and carving back would quietly put a stone lid
// over every "open" room.
static void carveRoom(level::Level& lvl, int x0, int z0, int x1, int z1,
                      int floorY, int ceilY, bool roofed) {
    // The solid mass is one block WIDER than the room on every side, so
    // the room really is enclosed by stone and cannot leak light in from
    // whatever the generator happened to leave next to it.
    fill(lvl, x0 - 1, floorY - 1, z0 - 1, x1 + 1, ceilY, z1 + 1, STONE);
    fill(lvl, x0, floorY, z0, x1, ceilY - 1, z1, AIR);
    fill(lvl, x0, ceilY, z0, x1, ceilY, z1, roofed ? STONE : AIR);
}

// Clears a box of sky above `yTop` so nothing is capped by leftover
// terrain or by the stone mass the tests build.
static void clearSky(level::Level& lvl, int x0, int z0, int x1, int z1,
                     int yTop) {
    fill(lvl, x0, yTop, z0, x1, 127, z1, AIR);
}

// ---- 1 + 4. torch light, and all six directions --------------------------
static void testBlockLight() {
    printf("1. block light exists and spreads in the six directions\n");
    auto cfg = makeConfig("light_torch", 11);
    level::Level lvl(cfg);
    (void)lvl.chunks().getChunk(0, 0);

    // A sealed pocket inside solid rock: no sky can possibly reach it.
    carveRoom(lvl, 4, 4, 12, 12, 65, 71, /*roofed=*/true);
    drain(lvl);

    check(skyAt(lvl, 8, 68, 8) == 0,
          "a sealed pocket starts with no sky light (0)");
    check(blockLightAt(lvl, 8, 68, 8) == 0,
          "and no block light either");

    const uint8_t torchEmission = mcp_block_light_emission((uint8_t)blk::BlockId::TORCH);
    check(torchEmission > 0, "a torch is registered as a light source");

    set(lvl, 8, 68, 8, (uint8_t)blk::BlockId::TORCH);
    drain(lvl);

    const uint8_t at = blockLightAt(lvl, 8, 68, 8);
    check(at == torchEmission,
          "the torch's own cell carries its full emission");
    check(at == 0 || blockLightAt(lvl, 9, 68, 8) == at - 1,
          "light drops by one one block away");

    // The old code only ever stepped sideways. Check all six.
    const int dx[6] = {1, -1, 0, 0, 0, 0};
    const int dy[6] = {0, 0, 1, -1, 0, 0};
    const int dz[6] = {0, 0, 0, 0, 1, -1};
    static const char* name[6] = {"+x", "-x", "+y (up)", "-y (down)",
                                  "+z", "-z"};
    bool allSix = true;
    for (int d = 0; d < 6; ++d) {
        const uint8_t v = blockLightAt(lvl, 8 + dx[d], 68 + dy[d], 8 + dz[d]);
        printf("        neighbour %-10s -> block light %d\n", name[d], (int)v);
        if (v == 0) allSix = false;
    }
    check(allSix, "light reaches all six neighbours, including up and down");

    const uint8_t far = blockLightAt(lvl, 11, 68, 8);
    check(far > 0 && far < at, "and fades out with distance rather than all-or-nothing");

    // Breaking the torch has to take the light away again.
    set(lvl, 8, 68, 8, AIR);
    drain(lvl);
    check(blockLightAt(lvl, 9, 68, 8) == 0 && blockLightAt(lvl, 8, 68, 8) == 0,
          "breaking the torch removes the light it produced");
}

// ---- 2. light reaches DOWN under an overhang ---------------------------
static void testLightDescends() {
    printf("2. sky light travels down under an overhang\n");
    auto cfg = makeConfig("light_overhang", 12);
    level::Level lvl(cfg);
    (void)lvl.chunks().getChunk(0, 0);

    // Floor at y=64, ceiling slab at y=70 covering x/z 1..14, open sky above
    // it. Only the border ring (x=0, x=15, z=0, z=15) is uncapped, so
    // anything lit under the slab got there by travelling SIDEWAYS and
    // then DOWN — which the old 4-way flood could not do.
    clearSky(lvl, 0, 0, 15, 15, 71);
    fill(lvl, 0, 64, 0, 15, 70, 15, STONE);
    fill(lvl, 0, 65, 0, 15, 69, 15, AIR);
    fill(lvl, 1, 70, 1, 14, 70, 14, STONE);
    // Open the border ring.
    fill(lvl, 0, 70, 0, 15, 70, 0, AIR);
    fill(lvl, 0, 70, 15, 15, 70, 15, AIR);
    fill(lvl, 0, 70, 1, 0, 70, 14, AIR);
    fill(lvl, 15, 70, 1, 15, 70, 14, AIR);
    drain(lvl);

    const uint8_t open = skyAt(lvl, 0, 68, 8);
    check(open > 10, "the open border column is brightly lit");

    const uint8_t under = skyAt(lvl, 8, 68, 8);
    printf("        under the slab at (8,68,8): sky light = %d\n", (int)under);
    check(under > 0,
          "a cell under the slab is lit (the old 4-way flood left it at 0)");

    const uint8_t deeper = skyAt(lvl, 8, 66, 8);
    printf("        two blocks lower,  (8,66,8): sky light = %d\n", (int)deeper);
    check(deeper > 0, "light keeps travelling downward, not just sideways");

    // Directly beneath the stone the light must be blocked.
    check(skyAt(lvl, 8, 70, 8) == 0 ||
              skyAt(lvl, 8, 70, 8) < skyAt(lvl, 8, 69, 8),
          "the stone of the slab itself does not let light through");
}

// ---- 3. placing and breaking a ceiling updates the light -----------------
static void testLightUpdates() {
    printf("3. building and breaking a ceiling updates the lighting\n");
    auto cfg = makeConfig("light_update", 13);
    level::Level lvl(cfg);
    (void)lvl.chunks().getChunk(0, 0);

    carveRoom(lvl, 2, 2, 13, 13, 65, 71, /*roofed=*/false);
    drain(lvl);

    const uint8_t bright = skyAt(lvl, 8, 68, 8);
    printf("        open room, (8,68,8): sky light = %d\n", (int)bright);
    check(bright >= 12, "an open room is brightly lit");

    // Drop a roof over it — the room's ceiling row is at ceilY (71).
    fill(lvl, 2, 71, 2, 13, 71, 13, STONE);
    drain(lvl);
    const uint8_t roofed = skyAt(lvl, 8, 68, 8);
    printf("        after roofing it over: sky light = %d\n", (int)roofed);
    check(roofed < bright,
          "putting a ceiling on darkens the room (it used to stay at full sun)");
    check(roofed == 0,
          "a fully enclosed room goes completely dark, like vanilla");

    // Punch a one-block hole in the roof: light must come back down
    // through it, which is the vertical propagation the old pass lacked.
    set(lvl, 8, 71, 8, AIR);
    drain(lvl);
    const uint8_t throughHole = skyAt(lvl, 8, 68, 8);
    printf("        after a hole in the roof: sky light = %d\n",
           (int)throughHole);
    check(throughHole > 0, "a hole in the roof lets light down into the room");
    // A one-block hole is a direct sunbeam, so the shaft itself stays at
    // 15 all the way down (vanilla does the same). What attenuates is
    // everything BESIDE it, which is lit only by spread light.
    const uint8_t besideHole = skyAt(lvl, 9, 68, 8);
    printf("        next to the hole: sky light = %d\n", (int)besideHole);
    check(besideHole > 0 && besideHole < throughHole,
          "the shaft is bright but the cells beside it are only spread-lit");

    // Take the whole roof off again.
    fill(lvl, 2, 71, 2, 13, 71, 13, AIR);
    drain(lvl);
    const uint8_t reopened = skyAt(lvl, 8, 68, 8);
    printf("        after removing the roof: sky light = %d\n", (int)reopened);
    check(reopened > roofed, "removing the ceiling brightens the room again");
    check(reopened >= 12, "and returns it to full daylight");
}

// ---- 4. light crosses the chunk border ----------------------------------
static void testChunkBorder() {
    printf("4. light carries across the chunk border\n");
    auto cfg = makeConfig("light_border", 14);
    level::Level lvl(cfg);
    (void)lvl.chunks().getChunk(0, 0);
    (void)lvl.chunks().getChunk(1, 0);   // x = 16..31
    drain(lvl);
    check(lvl.isChunkLoaded(0, 0) && lvl.isChunkLoaded(1, 0),
          "both chunks of the border test are resident");

    // A long roofed hall whose only opening is at x=0..7, which lies in
    // chunk 0. The lit part of chunk 1 is reachable ONLY by light that
    // crossed the 15/16 border — per-chunk lighting left it black.
    clearSky(lvl, 0, 0, 31, 15, 71);
    fill(lvl, 0, 64, 0, 31, 70, 15, STONE);
    fill(lvl, 0, 65, 0, 31, 69, 15, AIR);
    fill(lvl, 0, 70, 0, 7, 70, 15, AIR);      // opening at x = 0..7 only
    fill(lvl, 8, 70, 0, 31, 70, 15, STONE);   // roof from x=8 to x=31
    drain(lvl);

    const uint8_t inside0 = skyAt(lvl, 10, 68, 8);
    const uint8_t across = skyAt(lvl, 16, 68, 8);
    printf("        (10,68,8) = %d   (16,68,8) = %d\n", (int)inside0,
           (int)across);
    printf("        profile y=68,z=8:");
    for (int x = 6; x <= 20; ++x) printf(" %d", (int)skyAt(lvl, x, 68, 8));
    printf("\n");
    check(inside0 > 0, "the lit stretch in chunk 0 is lit");
    check(across > 0,
          "chunk 1 is lit by light crossing the border (was black before)");
    check(across < inside0, "and it fades on the way, as vanilla does");
}

// ---- 5. cost -----------------------------------------------------------
static void testCost() {
    printf("5. relighting a chunk stays cheap\n");
    auto cfg = makeConfig("light_cost", 15);
    level::Level lvl(cfg);
    (void)lvl.chunks().getChunk(0, 0);
    carveRoom(lvl, 2, 2, 13, 13, 65, 71, false);
    drain(lvl);

    // A single block placement is the worst case a player can trigger.
    const auto t0 = std::chrono::steady_clock::now();
    const int kIters = 200;
    for (int i = 0; i < kIters; ++i)
        set(lvl, 8, 68, 8, (i & 1) ? STONE : AIR);
    const auto t1 = std::chrono::steady_clock::now();
    const double us =
        std::chrono::duration<double, std::micro>(t1 - t0).count() / kIters;
    printf("        relight on block change: %.1f us\n", us);
    check(us < 5000.0,
          "a block placement costs well under 5 ms of relighting");

    // And the per-tick budget for newly loaded chunks.
    for (int cx = -2; cx <= 2; ++cx)
        for (int cz = -2; cz <= 2; ++cz) {
            (void)lvl.chunks().getChunk(cx, cz);
            lvl.onChunkAdded(cx, cz);
        }
    const auto t2 = std::chrono::steady_clock::now();
    lvl.tick(1000);   // one tick = the per-tick budget
    const auto t3 = std::chrono::steady_clock::now();
    const double tickUs =
        std::chrono::duration<double, std::micro>(t3 - t2).count();
    printf("        one tick of pending chunk relights: %.1f us\n", tickUs);
    check(tickUs < 20000.0,
          "one tick of chunk relighting stays under 20 ms");
}

// ---- 6. the generation pass on its own -----------------------------------
// Everything above goes through Level, which relights on the main thread.
// But the FIRST FullChunkDataPacket a player receives for a chunk carries
// whatever mcp_chunk_propagate_sky_light() computed at generation time, so
// that pass has to be right by itself — it is what made the client render
// black under overhangs before anything else had a chance to fix it.
static void testGenerationPass() {
    printf("6. the generation light pass is correct on its own\n");
    mcp_chunk_t* c = mcp_chunk_new(0, 0);
    if (!c) {
        printf("  [FAIL] could not allocate a chunk\n");
        ++failures;
        return;
    }
    auto put = [&](int x, int y, int z, uint8_t id) {
        mcp_chunk_set_block_full(c, x, y, z, id, 0);
    };
    auto sky = [&](int x, int y, int z) {
        return mcp_chunk_get_sky_light(c, x, y, z);
    };
    auto blk = [&](int x, int y, int z) {
        return mcp_chunk_get_block_light(c, x, y, z);
    };

    // Floor at y=64, hollow interior, roof slab at y=70 spanning 1..14,
    // leaving the x=0 border column open to the sky.
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 64; y <= 70; ++y) put(x, y, z, STONE);
    for (int x = 1; x < 15; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 65; y <= 69; ++y) put(x, y, z, AIR);
    // Leave the x=0 and x=15 border columns open all the way up, so they
    // are the only source of light for the covered interior.
    for (int x = 0; x <= 15; x += 15)
        for (int z = 0; z < 16; ++z)
            for (int y = 65; y <= 70; ++y) put(x, y, z, AIR);
    put(8, 68, 8, (uint8_t)blk::BlockId::TORCH);

    mcp_chunk_recalculate_height_map(c);
    mcp_chunk_propagate_sky_light(c);

    check(sky(0, 68, 0) > 10, "the open border column is lit");
    const uint8_t under = sky(8, 68, 8);
    printf("        under the slab (8,68,8): sky light = %d\n", (int)under);
    check(under > 0,
          "light reaches under the slab inside the generation pass too");

    const uint8_t bl = blk(8, 68, 8);
    check(bl == mcp_block_light_emission((uint8_t)blk::BlockId::TORCH),
          "the torch emits block light in the generation pass");
    check(blk(9, 68, 8) == bl - 1,
          "and that light spreads one block with the usual falloff");
    check(blk(8, 67, 8) == bl - 1,
          "including downwards");

    // And it must not be absurdly slow: this runs for every chunk.
    const auto t0 = std::chrono::steady_clock::now();
    const int kRuns = 50;
    for (int i = 0; i < kRuns; ++i) mcp_chunk_propagate_sky_light(c);
    const auto t1 = std::chrono::steady_clock::now();
    const double us =
        std::chrono::duration<double, std::micro>(t1 - t0).count() / kRuns;
    printf("        generation light pass: %.1f us/chunk\n", us);
    check(us < 3000.0, "the per-chunk generation light pass stays under 3 ms");

    mcp_chunk_destroy(c);
}

// ---- 7. the light actually reaches the client buffer ---------------------
// The engine can compute block light perfectly and the player still sees
// nothing if mcp_chunk_network_serialize() ships zeros. It used to: the
// block-light region was skipped with "all zeros", so every torch's light
// stopped at the server. This decodes the real packet layout.
static void testSerializedLight() {
    printf("7. the serialized chunk the client receives carries the light\n");
    mcp_chunk_t* c = mcp_chunk_new(0, 0);
    if (!c) {
        printf("  [FAIL] could not allocate a chunk\n");
        ++failures;
        return;
    }
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 128; ++y)
                mcp_chunk_set_block_full(c, x, y, z, STONE, 0);
    // Hollow room with one torch inside.
    for (int x = 4; x <= 11; ++x)
        for (int z = 4; z <= 11; ++z)
            for (int y = 65; y <= 70; ++y)
                mcp_chunk_set_block_full(c, x, y, z, AIR, 0);
    mcp_chunk_set_block_full(c, 8, 68, 8, (uint8_t)blk::BlockId::TORCH, 0);

    mcp_chunk_recalculate_height_map(c);
    mcp_chunk_propagate_sky_light(c);

    size_t len = 0;
    uint8_t* buf = mcp_chunk_network_serialize(c, &len);
    check(buf != nullptr, "the chunk serialized");
    if (!buf) { mcp_chunk_destroy(c); return; }

    // Layout: blocks | data | skylight | blocklight | heightmap | ...
    const size_t kBlocks = 32768, kData = 16384, kSky = 16384;
    const size_t blockLightOff = kBlocks + kData + kSky;
    auto nibble = [&](size_t regionOff, int x, int y, int z) {
        const uint32_t ci = (uint32_t)((x << 11) | (z << 7) | y);
        const uint8_t b = buf[regionOff + (ci >> 1)];
        return (b >> ((ci & 1) ? 4 : 0)) & 0x0F;
    };

    const uint8_t want =
        mcp_block_light_emission((uint8_t)blk::BlockId::TORCH);
    check(len >= blockLightOff + 16384, "the buffer is long enough for block light");
    const int sent = nibble(blockLightOff, 8, 68, 8);
    printf("        block light sent for the torch cell: %d (expected %d)\n",
           sent, (int)want);
    check(sent == want,
          "the torch's light level reaches the client buffer (was 0 before)");
    check(nibble(blockLightOff, 9, 68, 8) == sent - 1,
          "and so does the falloff next to it");

    // Sky light must still be intact in its own region: compare the bytes
    // against the chunk's own values rather than hard-coding an expected
    // number, so the check holds whatever geometry the test builds.
    bool skyMatches = true;
    for (int x = 0; x < 16; x += 5)
        for (int z = 0; z < 16; z += 5)
            for (int y = 0; y < 128; y += 17)
                if (nibble(kBlocks + kData, x, y, z) !=
                    mcp_chunk_get_sky_light(c, x, y, z))
                    skyMatches = false;
    check(skyMatches, "sky light is still serialized exactly as computed");

    free(buf);
    mcp_chunk_destroy(c);
}

int main() {
    printf("== light_check ==\n");
    // The block registry has to be populated: that is where light
    // emission and opacity come from. Server::start() does this too
    // (through its private registerBlocksAndItems()), so call the same
    // public entry point it uses.
    cursestone::block::register_all_blocks();

    testBlockLight();
    testLightDescends();
    testLightUpdates();
    testChunkBorder();
    testCost();
    testGenerationPass();
    testSerializedLight();

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    printf("%s\n", failures ? "LIGHT CHECK: FAIL" : "LIGHT CHECK: OK");
    return failures ? 1 : 0;
}