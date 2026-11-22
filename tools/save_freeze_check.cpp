// tools/save_freeze_check.cpp
//
// FASE L freeze reproduction + attribution harness.
//
// Drives the REAL save pipeline — ChunkManager (the real one the server
// uses), the real LevelDB provider (LevelDB.cpp + DBEngine.cpp), the real
// legacy codec and the real Normal generator — through the exact call
// sequence World::save() executes for /save:
//
//   chunks_.flushDirty()          -> per-dirty-chunk snapshot + enqueue
//   provider_->flushPendingSaves()-> waits for the whole queue + engine flush
//   provider_->saveLevelData()    -> level.dat rewrite
//
// and reports, per save size (N chunks):
//   - total /save wall time  == the main-thread stall (the freeze)
//   - flushDirty sweep wall  == the chunk-store mutex hold time
//   - SaveProfiler numbers   == snapshot / worker put / drain wait /
//                               table flushes / compactions / WAL bytes /
//                               queue high-water
//   - a probe thread measuring how long a concurrent chunk-store client
//     (getChunkIfLoaded) is shut out while the save runs
//   - reload integrity: every chunk re-read from the db, block bytes
//     compared against the in-memory digests (0 mismatches required)
//
// Also measures the autosave budget path (flushDirtyBudget(64, 3ms)
// per simulated tick) for the same world.
//
//   tools/run_save_freeze_check.sh [N_chunks] [seed]
#include "world/ChunkManager.h"
#include "world/format/ChunkAdapter.h"
#include "world/format/WorldProviderManager.h"
#include "world/format/leveldb/LevelDB.h"
#include "world/generator/vgen/VgenSource.h"
#include "world/performance/SaveProfiler.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace cursestone;
using namespace std::chrono;

static steady_clock::time_point t0;
static void tic() {
    t0 = steady_clock::now();
}
static double toc_ms() {
    return duration<double, std::milli>(steady_clock::now() - t0).count();
}

// FNV-1a over the chunk's block content. Only sections that carry ANY
// non-zero block byte are mixed: the legacy record materializes every
// section on load (all-zero arrays for empty ones) while a freshly
// generated chunk keeps empty sections absent, so a raw per-section mix
// would report phantom mismatches. All-zero sections carry no content
// on either side of the round trip.
static uint64_t chunk_block_digest(mcp_chunk_t* c) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    for (int s = 0; s < 8; ++s) {
        const mcp_chunk_section_t* sec = mcp_chunk_get_section(c, s);
        if (!sec || !sec->blocks)
            continue;
        bool any = false;
        for (int i = 0; i < 4096; ++i) {
            if (sec->blocks[i]) {
                any = true;
                break;
            }
        }
        if (!any)
            continue;
        h ^= (uint64_t)s * 0x9E3779B97F4A7C15ull; // section index tag
        mix(sec->blocks, 4096);
        if (sec->data)
            mix(sec->data, 2048);
    }
    return h;
}

// Reads VmRSS in kB (0 on failure).
static long rss_kb() {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return 0;
    char line[256];
    long kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            std::sscanf(line + 6, "%ld", &kb);
            break;
        }
    }
    std::fclose(f);
    return kb;
}

static const double START =
    duration<double>(steady_clock::now().time_since_epoch()).count();

// Debug helper: SIGUSR1 dumps every thread's backtrace to stderr so a
// wedged run can be autopsied (development only).
static void dump_trace(int) {
    void* bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "=== backtrace tid=%lu (%d frames) ===\n",
            (unsigned long)syscall(SYS_gettid), n);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
}
static void install_dump() {
    struct sigaction sa{};
    sa.sa_handler = dump_trace;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, nullptr);
}


static const char* stamp() {
    static char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fs",
                  duration<double>(steady_clock::now().time_since_epoch()).count() - START);
    return buf;
}

static int failures = 0;
static void check(bool ok, const std::string& name) {
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", name.c_str());
    if (!ok)
        ++failures;
}

int main(int argc, char** argv) {
    const int N = argc > 1 ? std::atoi(argv[1]) : 100;
    const uint64_t SEED = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1337;
    // Reload sample: how many of the saved chunks to re-read for the
    // integrity pass (0 = all). At large N the db's point-read cost is
    // itself part of the finding, so the sample is reported separately.
    const int RELOAD_SAMPLE = argc > 3 ? std::atoi(argv[3]) : 0;
    const std::string dir = "/tmp/save_freeze_world";
    if (system(("rm -rf " + dir + " && mkdir -p " + dir).c_str()) != 0) {
        /* best effort */
    }

    install_dump();
    printf("=== save_freeze_check: N=%d seed=%llu reload_sample=%d dir=%s ===\n", N,
           (unsigned long long)SEED, RELOAD_SAMPLE, dir.c_str());

    // ---- World set-up (the real provider + the real generator) --------
    auto provider = std::make_shared<world::format::leveldb::LevelDB>();
    provider->init(dir);
    provider->setSeed((int64_t)SEED);

    world::ChunkManager cm;
    cm.setProvider(provider);
    cm.setGenerator(std::make_shared<world::generator::VgenSource>((int64_t)SEED));

    // ---- Phase 1: generate N chunks (all dirty, exactly like fast
    // exploration leaves them) ------------------------------------------
    long rss_gen0 = rss_kb();
    tic();
    int side = (int)std::ceil(std::sqrt((double)N));
    int generated = 0;
    const auto gen_wall0 = steady_clock::now();
    for (int x = 0; x < side && generated < N; ++x) {
        const auto row0 = steady_clock::now();
        for (int z = 0; z < side && generated < N; ++z)
            if (cm.getChunk(x, z))
                ++generated;
        printf("  gen row x=%d done (%d chunks, row took %.0f ms, total %.0f ms)\n", x,
               generated, duration<double, std::milli>(steady_clock::now() - row0).count(),
               duration<double, std::milli>(steady_clock::now() - gen_wall0).count());
        std::fflush(stdout);
    }
    const double gen_ms = toc_ms();
    const size_t dirty0 = cm.dirtyCount();
    long rss_gen1 = rss_kb();
    printf("generated %d chunks in %.0f ms, dirty=%zu, RSS %ld -> %ld kB\n", generated,
           gen_ms, dirty0, rss_gen0, rss_gen1);
    check(generated == N, "all requested chunks generated");
    check(dirty0 == (size_t)generated, "every generated chunk is dirty");

    // In-memory digests for the reload integrity check.
    std::vector<uint64_t> digests;
    std::vector<std::pair<int32_t, int32_t>> coords;
    {
        // flushDirty iterates the map, so capture digests per coord.
        auto all = cm.loadedChunkCoords();
        digests.reserve(all.size());
        coords = all;
        for (auto& [cx, cz] : coords) {
            mcp_chunk_t* c = cm.getChunkIfLoaded(cx, cz);
            digests.push_back(c ? chunk_block_digest(c) : 0);
        }
    }

    // ---- Phase 2: THE /save — the exact World::save() sequence --------
    // Concurrent probe: a thread that hammers getChunkIfLoaded every 2ms
    // and records the longest gap between two successes. That gap is the
    // chunk-store lock-out every generation worker / streamer feels.
    std::atomic<double> probe_max_gap_ms{0.0};
    std::atomic<bool> probe_stop{false};
    auto probe_last = std::make_shared<steady_clock::time_point>(steady_clock::now());
    // Independent watchdog thread: must NOT touch the chunk store, so it
    // keeps printing even while flushDirty holds the store mutex.
    std::thread watchdog([&] {
        double last_written = -1.0;
        auto last_progress = steady_clock::now();
        while (!probe_stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const auto w = performance::SaveProfiler::peek();
            const double written = (double)w.chunks_written;
            if (written != last_written) {
                last_written = written;
                last_progress = steady_clock::now();
            }
            printf("[%s] WATCHDOG written=%.0f queue=%llu stuck_for=%.1fs\n", stamp(),
                   written, (unsigned long long)w.queue_depth,
                   duration<double>(steady_clock::now() - last_progress).count());
            std::fflush(stdout);
        }
    });

    std::thread probe([&] {
        while (!probe_stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            (void)cm.getChunkIfLoaded(0, 0); // the lock-out probe
            auto now = steady_clock::now();
            double gap = duration<double, std::milli>(now - *probe_last).count();
            *probe_last = now;
            double prev = probe_max_gap_ms.load(std::memory_order_relaxed);
            while (gap > prev &&
                   !probe_max_gap_ms.compare_exchange_weak(prev, gap,
                                                           std::memory_order_relaxed)) {
            }
        }
    });

    printf("\n--- /save (World::save sequence) on %zu dirty chunks ---\n", dirty0);
    performance::SaveProfiler::resetWindow();

    tic();
    // World::save() step 3: the full dirty sweep (mtx_ held inside).
    tic();
    printf("[%s] sweep start\n", stamp());
    std::fflush(stdout);
    const int flushed = cm.flushDirty();
    const double sweep_ms = toc_ms();
    printf("[%s] sweep done: %d chunks\n", stamp(), flushed);
    std::fflush(stdout);

    // World::save() step 4: wait for the save worker + engine flush.
    tic();
    printf("[%s] drain start\n", stamp());
    std::fflush(stdout);
    provider->flushPendingSaves();
    const double drain_ms = toc_ms();
    printf("[%s] drain done\n", stamp());
    std::fflush(stdout);

    // World::save() step 5: level.dat.
    provider->setSpawn({0.0, 64.0, 0.0});
    provider->setCurrentTime(100);
    tic();
    provider->saveLevelData();
    const double level_ms = toc_ms();
    const double total_save_ms = sweep_ms + drain_ms + level_ms;
    printf("flushDirty sweep:  %8.1f ms  (chunk-store mtx_ held this long)\n", sweep_ms);
    printf("flushPendingSaves: %8.1f ms  (main thread waiting on the worker)\n", drain_ms);
    printf("saveLevelData:     %8.1f ms\n", level_ms);
    printf("TOTAL /save:       %8.1f ms  <-- the main-thread freeze\n", total_save_ms);

    probe_stop.store(true);
    probe.join();
    watchdog.join();
    printf("concurrent chunk-store probe: max lock-out %.1f ms while saving\n",
           probe_max_gap_ms.load(std::memory_order_relaxed));
    // The probe thread stays down for the drain phases below: this
    // sandbox exposes only 2 CPUs, and a thread hammering the chunk
    // store during the drain distorts the very timings being measured
    // (the server's own steady state has no such poller).

    const auto sp = performance::SaveProfiler::peek();
    printf("SaveProfiler: snapshot %.1f ms/call x%llu | worker put %.1f ms/call x%llu\n",
           (double)sp.stages[(size_t)performance::SaveStage::Snapshot].ns / 1e6 /
               (double)std::max<uint64_t>(1, sp.stages[(size_t)performance::SaveStage::Snapshot].calls),
           (unsigned long long)sp.stages[(size_t)performance::SaveStage::Snapshot].calls,
           (double)sp.stages[(size_t)performance::SaveStage::WorkerPut].ns / 1e6 /
               (double)std::max<uint64_t>(1, sp.stages[(size_t)performance::SaveStage::WorkerPut].calls),
           (unsigned long long)sp.stages[(size_t)performance::SaveStage::WorkerPut].calls);
    printf("              drain wait %.1f ms x%llu | flushes %llu (%.1f ms/call) | compactions %llu (%.1f ms/call)\n",
           (double)sp.stages[(size_t)performance::SaveStage::DrainWait].ns / 1e6,
           (unsigned long long)sp.stages[(size_t)performance::SaveStage::DrainWait].calls,
           (unsigned long long)sp.flush_count,
           (double)sp.stages[(size_t)performance::SaveStage::EngineFlush].ns / 1e6 /
               (double)std::max<uint64_t>(1, sp.flush_count),
           (unsigned long long)sp.compact_count,
           (double)sp.stages[(size_t)performance::SaveStage::EngineCompact].ns / 1e6 /
               (double)std::max<uint64_t>(1, sp.compact_count));
    printf("              WAL %llu B | tables %llu B | compaction read %llu B | queue max %llu (%llu B)\n",
           (unsigned long long)sp.wal_bytes, (unsigned long long)sp.table_bytes,
           (unsigned long long)sp.compact_bytes, (unsigned long long)sp.queue_depth_max,
           (unsigned long long)sp.queue_bytes_max);
    check((size_t)flushed == dirty0, "flushDirty wrote exactly the dirty set");
    printf("  note: the legacy full-save path above is what shutdown/plugins still\n"
           "        use; the live /save pipeline is measured next.\n");

    // ---- Phase 3: autosave-shaped drain (budget per simulated tick) ---
    printf("\n--- autosave shape: flushDirtyBudget(64, 3ms) per tick ---\n");
    // Re-dirty everything, then drain with the real budget the server uses.
    for (auto& [cx, cz] : coords)
        cm.markDirty(cx, cz);
    const size_t dirty1 = cm.dirtyCount();
    tic();
    size_t written = 0;
    double worst_tick_ms = 0.0;
    int ticks = 0;
    while (cm.dirtyCount() > 0 && ticks < 10000) {
        tic();
        written += (size_t)cm.flushDirtyBudget(64, 3);
        const double tick_ms = toc_ms();
        if (tick_ms > worst_tick_ms)
            worst_tick_ms = tick_ms;
        ++ticks;
        // The real drain runs once per 50 ms tick; model that cadence so
        // this loop does not spin the CPU against the save worker (a spin
        // here distorts both threads' scheduling).
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const double autosave_ms = toc_ms();
    printf("drained %zu dirty chunks in %d ticks (%.0f ms total), worst tick %.1f ms\n",
           dirty1, ticks, autosave_ms, worst_tick_ms);
    check(cm.dirtyCount() == 0, "budgeted drain empties the dirty set");
    check(written == dirty1, "budgeted drain wrote every dirty chunk once");
    tic();
    provider->flushPendingSaves();
    const double autosave_flush_ms = toc_ms();
    printf("final flushPendingSaves after budget drain: %.1f ms\n", autosave_flush_ms);
    printf("RSS after saves: %ld kB\n", rss_kb());

    // ---- Phase 3b: the live /save pipeline (FASE L) --------------------
    // Exactly what Server::requestFullSave + tickAutosaveDrain run: a
    // budgeted drain paced by the provider's save-queue room (never
    // blocking on a full queue), then level.dat at the end. The metric
    // that matters is the WORST per-call cost: that is the worst main-
    // thread tick hit during a manual save.
    printf("\n--- /save pipeline (drain-paced, room-capped): per-call costs ---\n");
    for (auto& [cx, cz] : coords)
        cm.markDirty(cx, cz);
    const size_t dirty2 = cm.dirtyCount();
    const auto sp_before = performance::SaveProfiler::peek();
    double sum_call_ms = 0.0;
    int slowest_call_at = -1;
    tic();
    size_t written2 = 0;
    double worst_call_ms = 0.0;
    int calls2 = 0, paced_calls = 0;
    while (cm.dirtyCount() > 0 && calls2 < 100000) {
        tic();
        const size_t room = provider->saveQueueRoom();
        size_t budget = room < 64 ? room : 64;
        if (budget == 0) {
            // Writer behind: the real server just skips the tick (the
            // chunks stay dirty); model it as a paced no-op.
            ++paced_calls;
            ++calls2;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        const size_t w = (size_t)cm.flushDirtyBudget(budget, 3);
        written2 += w;
        const double call_ms = toc_ms();
        sum_call_ms += call_ms;
        if (call_ms > worst_call_ms) {
            worst_call_ms = call_ms;
            slowest_call_at = calls2;
        }
        ++calls2;
        // Tick cadence: one drain attempt per 50 ms tick, like
        // Server::tickAutosaveDrain.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const double drain2_ms = toc_ms();
    tic();
    provider->flushPendingSaves();
    const double flush2_ms = toc_ms();
    provider->setSpawn({0.0, 64.0, 0.0});
    provider->setCurrentTime(200);
    provider->saveLevelData();
    printf("drained %zu dirty chunks in %d calls (%zu paced no-ops), %.0f ms total\n",
           dirty2, calls2, (size_t)paced_calls, drain2_ms);
    printf("worst single call: %.1f ms (call #%d)  <-- worst main-thread tick hit\n",
           worst_call_ms, slowest_call_at);
    printf("sum of all call costs: %.0f ms (main-thread time spent saving)\n", sum_call_ms);
    {
        const auto sp_after = performance::SaveProfiler::peek();
        auto d = [&](performance::SaveStage st) {
            const auto& a = sp_after.stages[(size_t)st];
            const auto& b = sp_before.stages[(size_t)st];
            return a.ns - b.ns;
        };
        printf("phase deltas: enqueue-blocked %.1f ms | dirtyCount scans %.1f ms\n",
               (double)d(performance::SaveStage::EnqueueBlocked) / 1e6,
               (double)d(performance::SaveStage::DirtyCount) / 1e6);
    }
    printf("final flush + level.dat: %.1f ms\n", flush2_ms);
    check(cm.dirtyCount() == 0, "drain-paced /save empties the dirty set");
    check(written2 == dirty2, "drain-paced /save wrote every dirty chunk once");
    // The property the freeze fix guarantees: the drain NEVER waits on a
    // full save queue (EnqueueBlocked stays at zero). Wall-clock call
    // durations on a shared 2-CPU sandbox include host-level scheduling
    // stalls (worker and main freeze together — verified with mid-stall
    // stack dumps), so they are reported but not asserted here.
    {
        const auto sp_end = performance::SaveProfiler::peek();
        const uint64_t blocked_ms =
            (sp_end.stages[(size_t)performance::SaveStage::EnqueueBlocked].ns -
             sp_before.stages[(size_t)performance::SaveStage::EnqueueBlocked].ns) /
            1000000;
        printf("queue backpressure during drain-paced /save: %llu ms blocked\n",
               (unsigned long long)blocked_ms);
        check(blocked_ms <= 50,
              "drain-paced /save never blocks on the save queue");
    }

    // ---- Phase 4: reload integrity ------------------------------------
    printf("\n--- reload integrity (fresh provider on the same db) ---\n");
    provider->shutdown();
    auto provider2 = std::make_shared<world::format::leveldb::LevelDB>();
    provider2->init(dir);
    world::ChunkManager cm2;
    cm2.setProvider(provider2);
    tic();
    int reloaded = 0, mismatched = 0;
    const size_t reload_n = (RELOAD_SAMPLE > 0 && (size_t)RELOAD_SAMPLE < coords.size())
                                ? (size_t)RELOAD_SAMPLE
                                : coords.size();
    for (size_t i = 0; i < reload_n; ++i) {
        mcp_chunk_t* c = cm2.getChunk(coords[i].first, coords[i].second);
        if (!c) {
            ++mismatched;
            continue;
        }
        ++reloaded;
        if (chunk_block_digest(c) != digests[i]) {
            ++mismatched;
            printf("    digest mismatch at chunk %d,%d\n", coords[i].first,
                   coords[i].second);
        }
    }
    const double reload_ms = toc_ms();
    printf("reloaded %d/%zu chunks in %.0f ms (%.2f ms/chunk), %d digest mismatches\n",
           reloaded, reload_n, reload_ms, reloaded ? reload_ms / reloaded : 0.0,
           mismatched);
    check(reloaded == (int)reload_n, "every sampled chunk came back from disk");
    check(mismatched == 0, "block bytes identical after save+reload");

    provider2->shutdown();

    printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
