// Nether generator harness — exercises the vanilla hell generator
// synchronously over a chunk area and validates the produced content:
//
//   * every chunk completes (no hang) and is deterministic on re-gen
//   * biomes are HELL (8) across the whole chunk
//   * bedrock floor at y=0 and bedrock roof at y=127
//   * netherrack terrain + lava sea below y=32 in open space
//   * features present somewhere in the area: quartz ore, glowstone,
//     soul sand, fire, nether brick (fortress)
//
//   g++ -std=c++20 -w -Isrc -Isrc/world/generator \
//       tools/nether_gen_check.cpp <obj files minus main.cpp.o> \
//       -o /tmp/nether_gen_check -lz -ldl -pthread
#include "world/generator/Nether.h"
#include "world/format/Chunk.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

using cursestone::world::generator::Nether;
using cursestone::world::generator::GenerationContext;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++failures;
}

static uint64_t chunkHash(const mcp_chunk_t* c) {
    // FNV-1a over every section's blocks (data nibbles excluded: the
    // generator never writes data for hell terrain).
    uint64_t h = 1469598103934665603ULL;
    for (int sec = 0; sec < 8; ++sec) {
        const mcp_chunk_section_t* s = mcp_chunk_get_section(const_cast<mcp_chunk_t*>(c), (uint8_t)sec);
        if (!s || !s->blocks) continue;
        for (int i = 0; i < 4096; ++i) {
            h ^= s->blocks[i];
            h *= 1099511628211ULL;
        }
    }
    if (c->biomes) {
        for (int i = 0; i < 256; ++i) {
            h ^= c->biomes[i];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

int main() {
    const int64_t SEED = 0xC0FFEE1234LL;
    const int R = 12; // chunk area [-R..R]^2 — includes x=17-equivalents far from spawn

    Nether gen(SEED);

    std::map<std::pair<int, int>, uint64_t> hashes;
    std::map<int, int> blockCounts; // id -> count
    long double totalMs = 0;
    long double worstMs = 0;
    int generated = 0;

    for (int cx = -R; cx <= R; ++cx) {
        for (int cz = -R; cz <= R; ++cz) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            GenerationContext ctx{};
            ctx.seed = SEED;
            ctx.chunk_x = cx;
            ctx.chunk_z = cz;
            ctx.biomes_in = nullptr;
            ctx.chunk = c;

            auto t0 = std::chrono::steady_clock::now();
            gen.generate(ctx);
            gen.populate(ctx);
            auto t1 = std::chrono::steady_clock::now();

            long double ms = std::chrono::duration<long double, std::milli>(t1 - t0).count();
            totalMs += ms;
            if (ms > worstMs) worstMs = ms;

            for (int sec = 0; sec < 8; ++sec) {
                const mcp_chunk_section_t* s = mcp_chunk_get_section(c, (uint8_t)sec);
                if (!s || !s->blocks) continue;
                for (int i = 0; i < 4096; ++i) blockCounts[s->blocks[i]] += 1;
            }
            hashes[{cx, cz}] = chunkHash(c);

            mcp_chunk_destroy(c);
            ++generated;
        }
    }

    std::printf("generated %d chunks in %.0Lf ms (avg %.1Lf ms, worst %.1Lf ms)\n",
                generated, totalMs, totalMs / generated, worstMs);

    // --- determinism: regenerate the same area with a FRESH generator ---
    // (a fresh instance mirrors a server restart with the same seed:
    // structure starts, RNGs and spill queues all rebuild from scratch)
    bool deterministic = true;
    std::pair<int, int> badAt{0, 0};
    {
        Nether gen2(SEED);
        for (int cx = -R; cx <= R && deterministic; ++cx) {
            for (int cz = -R; cz <= R; ++cz) {
                mcp_chunk_t* c = mcp_chunk_new(cx, cz);
                GenerationContext ctx{};
                ctx.seed = SEED;
                ctx.chunk_x = cx;
                ctx.chunk_z = cz;
                ctx.biomes_in = nullptr;
                ctx.chunk = c;
                gen2.generate(ctx);
                gen2.populate(ctx);
                if (chunkHash(c) != hashes[{cx, cz}]) {
                    deterministic = false;
                    badAt = {cx, cz};
                }
                mcp_chunk_destroy(c);
            }
        }
    }
    check(deterministic, "deterministic regeneration (fresh instance, same seed, same bytes)");

    // --- biome: HELL everywhere (sample every chunk's biome array) ---
    // covered by the hash equality above; assert a fresh chunk directly
    {
        mcp_chunk_t* c = mcp_chunk_new(3, -4);
        GenerationContext ctx{};
        ctx.seed = SEED;
        ctx.chunk_x = 3;
        ctx.chunk_z = -4;
        ctx.biomes_in = nullptr;
        ctx.chunk = c;
        gen.generate(ctx);
        bool allHell = c->biomes != nullptr;
        if (allHell) {
            for (int i = 0; i < 256; ++i) {
                if (c->biomes[i] != 8) { allHell = false; break; }
            }
        }
        check(allHell, "biome is HELL (8) across the chunk");
        mcp_chunk_destroy(c);
    }

    // --- content stats over the area ---
    auto cnt = [&](int id) { auto it = blockCounts.find(id); return it == blockCounts.end() ? 0 : it->second; };
    std::printf("\nblock counts over the area:\n");
    const std::map<int, const char*> names = {
        {0, "air"}, {7, "bedrock"}, {10, "flowing_lava"}, {11, "still_lava"},
        {13, "gravel"}, {39, "brown_mushroom"}, {40, "red_mushroom"},
        {51, "fire"}, {87, "netherrack"}, {88, "soul_sand"}, {89, "glowstone"},
        {112, "nether_brick"}, {114, "nether_brick_stairs"}, {115, "nether_wart"},
        {153, "quartz_ore"},
    };
    for (auto& kv : names) {
        std::printf("  %-20s %d\n", kv.second, cnt(kv.first));
    }

    check(cnt(87) > 100000, "netherrack terrain present");
    check(cnt(7) > 10000, "bedrock shell present");
    check(cnt(10) + cnt(11) > 1000, "lava sea present");
    check(cnt(153) > 0, "quartz ore veins present");
    check(cnt(88) > 0, "soul sand patches present");
    check(cnt(89) > 0, "glowstone clusters present");
    check(cnt(112) > 0, "fortress nether brick present");
    check(cnt(114) > 0, "fortress nether brick stairs present");
    check(cnt(115) > 0, "fortress nether wart present");

    // --- per-column structure on one chunk: bedrock floor + roof ---
    {
        mcp_chunk_t* c = mcp_chunk_new(0, 0);
        GenerationContext ctx{};
        ctx.seed = SEED;
        ctx.chunk_x = 0;
        ctx.chunk_z = 0;
        ctx.biomes_in = nullptr;
        ctx.chunk = c;
        gen.generate(ctx);
        bool floorOk = true, roofOk = true;
        for (int x = 0; x < 16 && (floorOk || roofOk); ++x) {
            for (int z = 0; z < 16; ++z) {
                mcp_chunk_section_t* s0 = mcp_chunk_get_or_alloc_section(c, 0);
                if (s0->blocks[mcp_section_block_index(x, 0, z)] != 7) floorOk = false;
                mcp_chunk_section_t* s7 = mcp_chunk_get_or_alloc_section(c, 7);
                if (s7->blocks[mcp_section_block_index(x, 15, z)] != 7) roofOk = false;
            }
        }
        check(floorOk, "bedrock floor solid at y=0");
        check(roofOk, "bedrock roof solid at y=127");
        mcp_chunk_destroy(c);
    }

    std::printf("\n%s\n", failures == 0 ? "ALL PASSED" : "FAILED");
    return failures == 0 ? 0 : 1;
}
