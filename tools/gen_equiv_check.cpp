/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// tools/gen_equiv_check.cpp — vanilla generation equivalence check.
//
// Generates a fixed block of chunks straight through the real vanilla
// generator (VgenSource: terrain + caves + populate, the exact bytes the
// server would stream) and prints one digest line per chunk plus a total.
// Two runs digest-equal => identical block+data bytes.
//
// Used three ways:
//   1. SIMD equivalence: same tree, CURSESTONE_NOISE_SCALAR=1 vs unset.
//   2. Vanilla preservation: current tree vs the baseline commit tree
//      (both trees print identical digests => generation untouched).
//   3. Concurrency order: `gen_equiv <seed> <radius> <threads>` runs the
//      same chunk set through N threads sharing ONE VgenSource — the
//      same shape the server runs (AsyncPool workers racing generate +
//      populate on neighbouring chunks, terrain cache and spill queues
//      coordinating). Every digest must equal the sequential run's or
//      a scheduling change altered vanilla output.
//
//   g++ -std=c++20 -O2 -w -Isrc -Isrc/world/generator \
//       -o gen_equiv tools/gen_equiv_check.cpp <sources, see run_gen_equiv.sh>
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"
#include "world/generator/Generator.h"
#include "world/generator/vgen/VgenSource.h"
#include "world/performance/NoiseOptimizer.h"

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

using cursestone::world::generator::GenerationContext;
using cursestone::world::generator::VgenSource;

static uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ULL) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// One chunk's full server-shaped pipeline, split exactly like the server's
// async tasks: phase 1 generates terrain (order-free), phase 2 takes the
// populate-gate turn (reference chunk order — see vgen/PopulateGate.h),
// populates, and flags. The chunk stays allocated — the caller digests
// (and frees) it only after every task finished, because cross-chunk
// feature spills that arrive after this chunk's populate are parked in
// the generator's spill queue and replayed at install time by the server
// (ChunkManager drains them before the chunk goes live). The drain step
// below is that install-time replay.
static mcp_chunk_t* generatePhase(VgenSource& gen, int64_t seed, int cx, int cz) {
    mcp_chunk_t* c = mcp_chunk_new(cx, cz);
    if (!c) {
        std::printf("FAIL: chunk alloc\n");
        std::exit(1);
    }
    for (int bx = 0; bx < 16; ++bx)
        for (int bz = 0; bz < 16; ++bz)
            mcp_chunk_set_biome(c, bx, bz, 1);

    GenerationContext ctx;
    ctx.seed = seed;
    ctx.chunk_x = cx;
    ctx.chunk_z = cz;
    ctx.biomes_in = nullptr;
    ctx.chunk = c;
    gen.generate(ctx);
    // Server task 1 also pre-creates the structure starts and records the
    // window-sharing dependencies the populate gate will wait on.
    gen.preparePopulateDeps(cx, cz);
    return c;
}

// One server-task-2 attempt: take the reference-order turn if it is free,
// otherwise report false so the caller re-queues (a worker that blocked
// waiting could deadlock the pool, exactly like the live pipeline).
static bool populatePhaseTry(VgenSource& gen, int64_t seed, mcp_chunk_t* c,
                             int cx, int cz) {
    GenerationContext ctx;
    ctx.seed = seed;
    ctx.chunk_x = cx;
    ctx.chunk_z = cz;
    ctx.biomes_in = nullptr;
    ctx.chunk = c;
    if (!gen.populateTryEnter(cx, cz))
        return false;
    gen.populate(ctx);
    gen.populateMarkDone(cx, cz);
    c->terrain_populated = true;
    c->dirty = true;
    mcp_chunk_recalculate_height_map(c);
    mcp_chunk_propagate_sky_light(c);
    return true;
}

static uint64_t digestChunk(mcp_chunk_t* c) {
    uint64_t h = 1469598103934665603ULL;
    for (int s = 0; s < 8; ++s) {
        const mcp_chunk_section_t* sec = mcp_chunk_get_section(c, (uint8_t)s);
        if (!sec || !sec->blocks) {
            h = fnv1a("air", 3, h);
            continue;
        }
        h = fnv1a(sec->blocks, 4096, h);
        h = fnv1a(sec->data ? sec->data : (const void*)"nodata", sec->data ? 2048 : 6, h);
    }
    if (c->biomes)
        h = fnv1a(c->biomes, 256, h);
    return h;
}

int main(int argc, char** argv) {
    const int64_t seed = argc > 1 ? atoll(argv[1]) : 12345;
    const int radius = argc > 2 ? atoi(argv[2]) : 6;  // chunks [-r, r]
    const int threads = argc > 3 ? atoi(argv[3]) : 1; // concurrent workers

    // Optional SIMD dispatch (the server enables the kernels via
    // PerformanceManager::init(); the check defaults to scalar so both
    // sides of the noise comparison are the scalar path unless -s is
    // passed). -s must produce digests IDENTICAL to the default: the
    // vector kernels are bit-exact or the runtime guard rejects them.
    if (argc > 4 && argv[4][0] == '-' && argv[4][1] == 's')
        cursestone::performance::noise::initialize();

    VgenSource gen(seed);

    const int side = 2 * radius + 1;
    std::vector<uint64_t> digests((size_t)side * side, 0);
    auto coordOf = [radius](size_t idx, int& cx, int& cz) {
        const int lx = (int)(idx % (size_t)(2 * radius + 1));
        const int lz = (int)(idx / (size_t)(2 * radius + 1));
        cx = lx - radius;
        cz = lz - radius;
    };

    // Register the whole workset with the populate gate up front, the way
    // the server's request burst does: a dependency that is queued but not
    // started must be visible as "not done" before any populate runs.
    for (size_t i = 0; i < digests.size(); ++i) {
        int cx, cz;
        coordOf(i, cx, cz);
        gen.populateRegister(cx, cz);
    }

    if (threads <= 1) {
        std::vector<mcp_chunk_t*> built(digests.size(), nullptr);
        for (int cx = -radius; cx <= radius; ++cx)
            for (int cz = -radius; cz <= radius; ++cz)
                built[(size_t)(cz + radius) * side + (cx + radius)] =
                    generatePhase(gen, seed, cx, cz);
        // Sequential = ascending chunk order (cx-major, the reference rank
        // order), so every gate turn is already free; populate in that
        // same pass. built[] is indexed (cz,radius)*side + (cx,radius).
        for (int cx = -radius; cx <= radius; ++cx)
            for (int cz = -radius; cz <= radius; ++cz)
                if (!populatePhaseTry(gen, seed,
                                      built[(size_t)(cz + radius) * side + (cx + radius)],
                                      cx, cz)) {
                    std::printf("FAIL: sequential turn blocked at %d %d\n", cx, cz);
                    return 1;
                }
        for (size_t i = 0; i < built.size(); ++i) {
            int cx, cz;
            coordOf(i, cx, cz);
            gen.drainSpills(cx, cz, built[i]); // install-time replay
            digests[i] = digestChunk(built[i]);
            mcp_chunk_destroy(built[i]);
        }
    } else {
        // Same shared generator, same task shape the server runs: N
        // workers generate in parallel, then N workers populate through
        // the gate — interacting chunks wait for their reference-order
        // turn, independent chunks run at once. The terrain cache
        // coordinates concurrent neighbour builds like the live pipeline.
        std::vector<mcp_chunk_t*> built(digests.size(), nullptr);
        // Phase 1: N workers generate in parallel (order-free).
        {
            std::atomic<size_t> next{0};
            auto worker = [&]() {
                for (;;) {
                    const size_t idx = next.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= digests.size())
                        return;
                    int cx, cz;
                    coordOf(idx, cx, cz);
                    built[idx] = generatePhase(gen, seed, cx, cz);
                }
            };
            std::vector<std::thread> pool;
            pool.reserve((size_t)threads);
            for (int t = 0; t < threads; ++t)
                pool.emplace_back(worker);
            for (auto& t : pool)
                t.join();
        }
        // Phase 2: N workers populate through the gate with the same
        // re-queue discipline as the server's task pipeline: a worker that
        // cannot take its turn hands the task back and picks the next one,
        // so no worker ever blocks (blocking could stall tasks still
        // sitting in the queue). Dependencies strictly point down-rank, so
        // the lowest unfinished populate is always runnable.
        {
            std::atomic<size_t> next{0};
            std::atomic<int> remaining{(int)digests.size()};
            std::mutex retryMtx;
            std::vector<size_t> retry; // kept sorted by reference rank, lowest first
            auto rankOf = [&](size_t idx) {
                int cx, cz;
                coordOf(idx, cx, cz);
                return vgen::populateRank(cx, cz);
            };
            auto worker = [&]() {
                for (;;) {
                    // Fresh tasks first (a dependency that is still unpulled
                    // would otherwise never start), then the re-queue in
                    // reference-rank order: the lowest-rank unfinished
                    // populate is always attempted first, so progress is
                    // guaranteed and no task starves the others.
                    size_t idx = SIZE_MAX;
                    const size_t got = next.fetch_add(1, std::memory_order_relaxed);
                    if (got < digests.size())
                        idx = got;
                    if (idx == SIZE_MAX) {
                        std::lock_guard<std::mutex> lk(retryMtx);
                        if (!retry.empty()) {
                            idx = retry.front();
                            retry.erase(retry.begin());
                        }
                    }
                    if (idx == SIZE_MAX) {
                        if (remaining.load(std::memory_order_relaxed) == 0)
                            return;
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                        continue;
                    }
                    // Pacing mirrors the server's FIFO re-queue cost.
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    int cx, cz;
                    coordOf(idx, cx, cz);
                    if (populatePhaseTry(gen, seed, built[idx], cx, cz)) {
                        remaining.fetch_sub(1, std::memory_order_relaxed);
                    } else {
                        const uint64_t r = rankOf(idx);
                        std::lock_guard<std::mutex> lk(retryMtx);
                        retry.insert(
                            std::lower_bound(retry.begin(), retry.end(), idx,
                                             [&](size_t a, size_t b) {
                                                 return rankOf(a) < rankOf(b);
                                             }),
                            idx);
                        (void)r;
                    }
                }
            };
            std::vector<std::thread> pool;
            pool.reserve((size_t)threads);
            for (int t = 0; t < threads; ++t)
                pool.emplace_back(worker);
            for (auto& t : pool)
                t.join();
            if (remaining.load() != 0) {
                std::printf("FAIL: populate pass left %d chunks\n",
                            remaining.load());
                return 1;
            }
        }
        // Install-time spill replay + digest, in the fixed coordinate
        // order, after every task finished — exactly what the server's
        // install path does before a chunk streams.
        for (size_t i = 0; i < built.size(); ++i) {
            int cx, cz;
            coordOf(i, cx, cz);
            gen.drainSpills(cx, cz, built[i]);
            digests[i] = digestChunk(built[i]);
            mcp_chunk_destroy(built[i]);
        }
    }

    uint64_t total = 1469598103934665603ULL;
    int chunks = 0;
    // Canonical cx-major coordinate order (the reference order the
    // sequential pass uses), independent of the internal storage map.
    for (int cx = -radius; cx <= radius; ++cx) {
        for (int cz = -radius; cz <= radius; ++cz) {
            const size_t i = (size_t)(cz + radius) * side + (cx + radius);
            std::printf("chunk %d %d %016llx\n", cx, cz,
                        (unsigned long long)digests[i]);
            total = fnv1a(&digests[i], sizeof(digests[i]), total);
            ++chunks;
        }
    }
    std::printf("TOTAL threads=%d chunks=%d seed=%lld radius=%d digest=%016llx\n",
                threads, chunks, (long long)seed, radius,
                (unsigned long long)total);
    return 0;
}
