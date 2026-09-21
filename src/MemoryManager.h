// MemoryManager — THE single global GarbageCollector for the whole
// server. There must be exactly ONE instance (Meyers singleton) and it
// is the only place that decides when memory is reclaimed.
//
// Why one global GC: the codebase used to have several scattered
// reclaimers (Server::tick's chunk-unload sweep, Level::tick's
// dropped-item sweep, Player::onUpdate's used_chunks_ cleanup, tile
// eviction) running on their own schedules. Independent schedules mean
// the world state players interact with (dropped items, open chests,
// loaded chunks) can disappear in the middle of a player action — the
// classic cause of pickup/inventory desyncs. Everything now funnels
// through collect(): the per-subsystem sweeps keep their cheap local
// bookkeeping, but any SWEEP THAT TOUCHES SHARED WORLD STATE only runs
// when the global GC authorizes it, on a single coordinated schedule.
#pragma once
#include <cstdint>
#include <atomic>
namespace atomikpe {
class MemoryManager {
public:
    static MemoryManager& get();

    // Config (from atomikpe.yml).
    void setLimitMB(int mb) { limit_mb_ = mb; }
    int limitMB() const { return limit_mb_; }
    // Interval between global GC passes, in ticks (20 TPS => 10s).
    void setGcIntervalTicks(uint64_t t) { gc_interval_ticks_ = t; }
    uint64_t gcIntervalTicks() const { return gc_interval_ticks_; }

    // Process RSS, as reported by /proc (0 when unavailable).
    int64_t currentUsageKB() const;

    // True when RSS exceeds the configured budget. Cheap enough to be
    // polled; the decision to actually collect is made in collect().
    bool shouldGc() const;

    // THE GC entry point — everything else must call this.
    // "force" runs the full sweep immediately (shutdown, /save, tests);
    // otherwise the pass is rate-limited by gc_interval_ticks_ so the
    // world can never be swept mid-tick by several unrelated callers.
    // Runs on the main thread only (all world registries are main-thread
    // or guarded there).
    void collect(bool force = false);

    // Legacy name kept for compatibility with existing call sites.
    void runGc() { collect(true); }

    // Diagnostics.
    uint64_t collectCount() const { return collects_.load(std::memory_order_relaxed); }
    uint64_t lastCollectTick() const { return last_collect_tick_.load(std::memory_order_relaxed); }

private:
    MemoryManager() = default;

    int limit_mb_ = 512;
    uint64_t gc_interval_ticks_ = 200;  // 10s at 20 TPS
    std::atomic<uint64_t> last_collect_tick_{0};
    std::atomic<uint64_t> collects_{0};
};
} // namespace
