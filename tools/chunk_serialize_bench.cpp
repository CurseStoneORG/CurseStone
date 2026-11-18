// Chunk wire serialize: equivalence check + benchmark.
//
// Correctness: mcp_chunk_snapshot_network_serialize(snapshot(chunk)) must
// produce byte-identical output to mcp_chunk_network_serialize(chunk) for
// every section layout the real world produces — all-present, partial,
// present-with-null-arrays, and missing sections.
//
// Benchmark: per-chunk main-thread cost of the OLD inline path (live
// serialize) vs the NEW async path (snapshot only — the transposition now
// runs on a worker).
#include "world/format/Chunk.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Fills a section with pseudo-random but deterministic data.
static void fill_section(mcp_chunk_section_t* s, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int i = 0; i < 4096; ++i)
        s->blocks[i] = (uint8_t)byte(rng);
    for (int i = 0; i < 2048; ++i)
        s->data[i] = (uint8_t)byte(rng);
    for (int i = 0; i < 2048; ++i)
        s->sky_light[i] = (uint8_t)byte(rng);
    for (int i = 0; i < 2048; ++i)
        s->block_light[i] = (uint8_t)byte(rng);
}

// Scenario matrix: [section][array] presence. Covers every branch of the
// serializer: missing sections (sky fills 0xFF), present sections with
// all arrays, and present sections with some NULL arrays.
static const bool kScenarios[][8] = {
    // clang-format off
    {true, true, true, true, true, true, true, true},    // full chunk
    {true, true, true, false, false, false, false, false}, // low half only
    {true, false, true, false, true, false, true, false},  // alternating
    {false, false, false, false, false, false, false, false}, // empty chunk
    // clang-format on
};
static const int kScenarioCount = 4;

int main() {
    int failures = 0;

    for (int sc = 0; sc < kScenarioCount; ++sc) {
        mcp_chunk_t* c = mcp_chunk_new(sc * 16, sc * 16);
        if (!c) {
            printf("FAIL: chunk alloc\n");
            return 1;
        }
        for (int i = 0; i < 8; ++i) {
            if (!kScenarios[sc][i])
                continue;
            c->sections[i] = mcp_chunk_get_or_alloc_section(c, (uint8_t)i);
            fill_section(c->sections[i], (unsigned)(sc * 100 + i));
            // Variant: present section with a NULL sky array (the
            // serializer reads zeros there, not 0xFF).
            if (sc == 2 && i == 4) {
                free(c->sections[i]->sky_light);
                c->sections[i]->sky_light = NULL;
            }
        }
        for (int i = 0; i < 256; ++i) {
            c->biomes[i] = (uint8_t)(i % 40);
            c->height_map[i] = (i * 7) % 128;
        }

        size_t live_len = 0;
        uint8_t* live = mcp_chunk_network_serialize(c, &live_len);
        mcp_chunk_wire_snapshot_t* snap = mcp_chunk_wire_snapshot_create(c);
        if (!snap) {
            printf("FAIL: snapshot alloc\n");
            return 1;
        }
        size_t snap_len = 0;
        uint8_t* from_snap = mcp_chunk_snapshot_network_serialize(snap, &snap_len);

        if (!live || !from_snap) {
            printf("FAIL: serialize returned null (scenario %d)\n", sc);
            ++failures;
        } else if (live_len != snap_len) {
            printf("FAIL: length mismatch (scenario %d): live %zu vs snapshot %zu\n", sc,
                   live_len, snap_len);
            ++failures;
        } else if (memcmp(live, from_snap, live_len) != 0) {
            printf("FAIL: byte mismatch (scenario %d)\n", sc);
            ++failures;
        } else {
            printf("OK scenario %d (%zu bytes, sections:", sc, live_len);
            for (int i = 0; i < 8; ++i)
                printf(" %d", kScenarios[sc][i] ? 1 : 0);
            printf(")\n");
        }

        free(live);
        free(from_snap);
        mcp_chunk_wire_snapshot_free(snap);
        mcp_chunk_destroy(c);
    }

    // ---- Benchmark ----
    mcp_chunk_t* c = mcp_chunk_new(0, 0);
    for (int i = 0; i < 8; ++i) {
        c->sections[i] = mcp_chunk_get_or_alloc_section(c, (uint8_t)i);
        fill_section(c->sections[i], (unsigned)i);
    }
    for (int i = 0; i < 256; ++i) {
        c->biomes[i] = 1;
        c->height_map[i] = 64;
    }

    const int kIter = 400;
    double live_ms = 0.0, snap_ms = 0.0, worker_ms = 0.0;
    for (int i = 0; i < kIter; ++i) {
        auto t0 = Clock::now();
        size_t len = 0;
        uint8_t* buf = mcp_chunk_network_serialize(c, &len);
        live_ms += ms_since(t0);
        free(buf);
    }
    for (int i = 0; i < kIter; ++i) {
        auto t0 = Clock::now();
        mcp_chunk_wire_snapshot_t* s = mcp_chunk_wire_snapshot_create(c);
        snap_ms += ms_since(t0);
        auto t1 = Clock::now();
        size_t len = 0;
        uint8_t* buf = mcp_chunk_snapshot_network_serialize(s, &len);
        worker_ms += ms_since(t1);
        mcp_chunk_wire_snapshot_free(s);
        free(buf);
    }
    printf("\nbench (%d iters, 8 allocated sections):\n", kIter);
    printf("  OLD main-thread cost (live serialize):     %7.3f ms/chunk\n", live_ms / kIter);
    printf("  NEW main-thread cost (snapshot only):      %7.3f ms/chunk\n", snap_ms / kIter);
    printf("  NEW worker cost (snapshot serialize):      %7.3f ms/chunk\n", worker_ms / kIter);
    printf("  tick saving per chunk:                     %7.3f ms\n",
           (live_ms - snap_ms) / kIter);

    mcp_chunk_destroy(c);
    if (failures == 0)
        printf("\nPASS: snapshot serialize is byte-identical to live serialize\n");
    return failures == 0 ? 0 : 1;
}
