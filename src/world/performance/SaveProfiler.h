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
// SaveProfiler — where save time actually goes.
//
// FASE L instrumentation: the autosave / /save / shutdown save pipeline
// (chunk snapshot -> save queue -> LevelDB worker -> WAL -> memtable ->
// table flush -> compaction) accumulates nanoseconds, call counts and
// byte counters into lock-free atomics, plus a few live gauges (queue
// depth, bytes queued, chunks pending). /status and the save diagnostics
// read the snapshot; nothing here influences save behavior or ordering.
//
// Overhead when enabled: two steady_clock reads plus a few atomic adds
// per save operation (per chunk, not per block) — noise next to the
// tens of microseconds a chunk snapshot costs. setEnabled(false) drops
// every probe to a single relaxed atomic load; production benchmarks
// run with it off.
//
// Threading: every counter is a relaxed atomic; producers run on the
// main thread (snapshot/enqueue/drain), the LevelDB worker (put/flush/
// compact) and the GC eviction path — none of them ever block on these.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace cursestone::performance {

// Nanosecond/call pairs, one per pipeline stage. Stages overlap on
// purpose (WorkerPut includes a table flush when the memtable crosses
// its threshold inside the batch) — read them as "what does each part
// cost", not as a partition.
enum class SaveStage : uint8_t {
    Snapshot,        // chunk snapshot + blob copy on the calling thread
    Enqueue,         // enqueueSave total, including blocked backpressure
    EnqueueBlocked,  // time the caller spent waiting for queue space
    WorkerPut,       // put_batch on the save worker (WAL + memtable)
    EngineFlush,     // memtable -> SSTable (+ fsync + WAL rotate + manifest)
    EngineCompact,   // full compaction (read every table, write merged)
    DrainWait,       // flushPendingSaves caller-side wait (the /save stall)
    EngineFlushCall, // explicit engine flush (drain end / close)
    FullFlushSweep,  // ChunkManager::flushDirty total (mtx_ held)
    BudgetFlushSweep,// ChunkManager::flushDirtyBudget total (mtx_ held)
    DirtyCount,      // dirtyCount() scan cost (polled by autosave/status)
    Count
};

class SaveProfiler {
  public:
    struct Entry {
        uint64_t ns = 0;
        uint64_t calls = 0;
    };
    struct Snapshot {
        Entry stages[(size_t)SaveStage::Count];
        // Live gauges and byte counters (non-destructive cumulative).
        uint64_t queue_depth = 0;       // save jobs waiting right now
        uint64_t queue_depth_max = 0;   // high-water mark since boot
        uint64_t queue_bytes = 0;       // bytes held by queued jobs (RAM)
        uint64_t queue_bytes_max = 0;   // high-water mark since boot
        uint64_t chunks_submitted = 0;  // jobs accepted into the queue
        uint64_t chunks_written = 0;    // batches the worker wrote to the db
        uint64_t write_failures = 0;    // put_batch rejections
        uint64_t wal_bytes = 0;         // bytes appended to the WAL
        uint64_t table_bytes = 0;       // bytes written into SSTables
        uint64_t compact_bytes = 0;     // bytes read back by compactions
        uint64_t flush_count = 0;       // memtable -> table transitions
        uint64_t compact_count = 0;     // full-merge compactions
        uint64_t enqueue_blocks = 0;    // callers that hit a full queue
    };

    // Probes are on by default (a few atomic adds per chunk save);
    // production benchmarks switch them off.
    static bool enabled() {
        return enabled_.load(std::memory_order_relaxed);
    }
    static void setEnabled(bool on) {
        enabled_.store(on, std::memory_order_relaxed);
    }

    static void add(SaveStage s, uint64_t ns) {
        if (!enabled())
            return;
        auto& e = stages_[(size_t)s];
        e.ns.fetch_add(ns, std::memory_order_relaxed);
        e.calls.fetch_add(1, std::memory_order_relaxed);
    }

    // --- gauge updates (cheap, called alongside the stages above) ------
    static void noteQueueDepth(size_t depth, size_t bytes) {
        queue_depth_.store(depth, std::memory_order_relaxed);
        queue_bytes_.store(bytes, std::memory_order_relaxed);
        if (!enabled())
            return;
        // High-water marks: single CAS loop, contended only while deep.
        uint64_t d = depth, db = queue_depth_max_.load(std::memory_order_relaxed);
        while (d > db && !queue_depth_max_.compare_exchange_weak(
                             db, d, std::memory_order_relaxed)) {
        }
        uint64_t b = bytes, bb = queue_bytes_max_.load(std::memory_order_relaxed);
        while (b > bb && !queue_bytes_max_.compare_exchange_weak(
                             bb, b, std::memory_order_relaxed)) {
        }
    }
    // Callers that found the queue full and had to wait (backpressure
    // events, counted separately from the gauge updates above).
    static void noteEnqueueBlocked() {
        enqueue_blocks_.fetch_add(1, std::memory_order_relaxed);
    }
    static void noteChunkSubmitted() {
        chunks_submitted_.fetch_add(1, std::memory_order_relaxed);
    }
    static void noteChunkWritten(bool ok) {
        (ok ? chunks_written_ : write_failures_).fetch_add(1, std::memory_order_relaxed);
    }
    static void noteWalBytes(uint64_t n) {
        wal_bytes_.fetch_add(n, std::memory_order_relaxed);
    }
    static void noteTableBytes(uint64_t n) {
        table_bytes_.fetch_add(n, std::memory_order_relaxed);
    }
    static void noteCompactBytes(uint64_t n) {
        compact_bytes_.fetch_add(n, std::memory_order_relaxed);
    }
    static void noteFlush() {
        flush_count_.fetch_add(1, std::memory_order_relaxed);
    }
    static void noteCompact() {
        compact_count_.fetch_add(1, std::memory_order_relaxed);
    }

    // Non-destructive read for /status and the save diagnostics; the
    // cumulative counters keep accumulating (diff two peeks to describe
    // a window). Stage entries are cumulative here, unlike
    // GenerationProfiler::drain — save windows are long, nobody wants
    // them erased by a status poll.
    static Snapshot peek() {
        Snapshot out;
        for (size_t i = 0; i < (size_t)SaveStage::Count; ++i) {
            out.stages[i].ns = stages_[i].ns.load(std::memory_order_relaxed);
            out.stages[i].calls = stages_[i].calls.load(std::memory_order_relaxed);
        }
        out.queue_depth = queue_depth_.load(std::memory_order_relaxed);
        out.queue_depth_max = queue_depth_max_.load(std::memory_order_relaxed);
        out.queue_bytes = queue_bytes_.load(std::memory_order_relaxed);
        out.queue_bytes_max = queue_bytes_max_.load(std::memory_order_relaxed);
        out.chunks_submitted = chunks_submitted_.load(std::memory_order_relaxed);
        out.chunks_written = chunks_written_.load(std::memory_order_relaxed);
        out.write_failures = write_failures_.load(std::memory_order_relaxed);
        out.wal_bytes = wal_bytes_.load(std::memory_order_relaxed);
        out.table_bytes = table_bytes_.load(std::memory_order_relaxed);
        out.compact_bytes = compact_bytes_.load(std::memory_order_relaxed);
        out.flush_count = flush_count_.load(std::memory_order_relaxed);
        out.compact_count = compact_count_.load(std::memory_order_relaxed);
        out.enqueue_blocks = enqueue_blocks_.load(std::memory_order_relaxed);
        return out;
    }

    // Reset the high-water marks and cumulative counters (used by the
    // save diagnostics to describe a fresh window). Queue depth/bytes
    // gauges are live values, not reset.
    static void resetWindow() {
        for (size_t i = 0; i < (size_t)SaveStage::Count; ++i) {
            stages_[i].ns.store(0, std::memory_order_relaxed);
            stages_[i].calls.store(0, std::memory_order_relaxed);
        }
        queue_depth_max_.store(queue_depth_.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
        queue_bytes_max_.store(queue_bytes_.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
        chunks_submitted_.store(0, std::memory_order_relaxed);
        chunks_written_.store(0, std::memory_order_relaxed);
        write_failures_.store(0, std::memory_order_relaxed);
        wal_bytes_.store(0, std::memory_order_relaxed);
        table_bytes_.store(0, std::memory_order_relaxed);
        compact_bytes_.store(0, std::memory_order_relaxed);
        flush_count_.store(0, std::memory_order_relaxed);
        compact_count_.store(0, std::memory_order_relaxed);
        enqueue_blocks_.store(0, std::memory_order_relaxed);
    }

    // Times one scope into one stage. Cheap enough to sprinkle freely.
    class Scope {
      public:
        explicit Scope(SaveStage s)
            : stage_(s),
              start_(SaveProfiler::enabled() ? std::chrono::steady_clock::now()
                                             : TimePoint{}) {}
        ~Scope() {
            if (!SaveProfiler::enabled())
                return;
            const auto end = std::chrono::steady_clock::now();
            SaveProfiler::add(
                stage_,
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_)
                    .count());
        }

      private:
        using TimePoint = std::chrono::steady_clock::time_point;
        SaveStage stage_;
        TimePoint start_;
    };

  private:
    struct AtomicEntry {
        // Zero-initialized by static storage; no NSDMI here so the
        // stages_ array can be declared inside the class body.
        std::atomic<uint64_t> ns;
        std::atomic<uint64_t> calls;
    };
    static inline std::atomic<bool> enabled_{true};
    static inline AtomicEntry stages_[(size_t)SaveStage::Count];
    static inline std::atomic<uint64_t> queue_depth_{0};
    static inline std::atomic<uint64_t> queue_depth_max_{0};
    static inline std::atomic<uint64_t> queue_bytes_{0};
    static inline std::atomic<uint64_t> queue_bytes_max_{0};
    static inline std::atomic<uint64_t> chunks_submitted_{0};
    static inline std::atomic<uint64_t> chunks_written_{0};
    static inline std::atomic<uint64_t> write_failures_{0};
    static inline std::atomic<uint64_t> wal_bytes_{0};
    static inline std::atomic<uint64_t> table_bytes_{0};
    static inline std::atomic<uint64_t> compact_bytes_{0};
    static inline std::atomic<uint64_t> flush_count_{0};
    static inline std::atomic<uint64_t> compact_count_{0};
    static inline std::atomic<uint64_t> enqueue_blocks_{0};
};

} // namespace cursestone::performance
