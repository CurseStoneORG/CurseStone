/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
/**
 * @brief Coordinates every memory-reclamation sweep in the server.
 *
 * THE single global GarbageCollector for the whole server. There must
 * be exactly ONE instance (Meyers singleton) and it is the only place
 * that decides when memory is reclaimed.
 *
 * Why one global GC: the codebase used to have several scattered
 * reclaimers (Server::tick's chunk-unload sweep, World::tick's
 * dropped-item sweep, Player::onUpdate's used_chunks_ cleanup, tile
 * eviction) running on their own schedules. Independent schedules mean
 * the world state players interact with (dropped items, open chests,
 * loaded chunks) can disappear in the middle of a player action - the
 * classic cause of pickup/inventory desyncs. Everything now funnels
 * through collect(): the per-subsystem sweeps keep their cheap local
 * bookkeeping, but any SWEEP THAT TOUCHES SHARED WORLD STATE only runs
 * when the global GC authorizes it, on a single coordinated schedule.
 */
#pragma once
#include <cstdint>
#include <atomic>
namespace cursestone {
class MemoryManager {
public:
    static MemoryManager& get();

    // Config (from cursestone.yml).
    void setLimitMB(int mb) { limit_mb_ = mb; }
    int limitMB() const { return limit_mb_; }
    // Interval between global GC passes, in ticks (20 TPS => 10s).
    void setGcIntervalTicks(uint64_t t) { gc_interval_ticks_ = t; }
    uint64_t gcIntervalTicks() const { return gc_interval_ticks_; }

    /**
     * @brief Reads the process resident set size (RSS).
     *
     * Reads VmRSS from /proc/self/status on Linux; returns 0 when the
     * value is unavailable (other platforms).
     *
     * @return RSS in kB, or 0 when unavailable.
     */
    int64_t currentUsageKB() const;

    /**
     * @brief Reports whether RSS exceeds the configured budget.
     *
     * Cheap enough to be polled; the decision to actually collect is
     * made by collect(). A non-positive limit disables the check.
     *
     * @return true when the budget is exceeded and collection may run.
     */
    bool shouldGc() const;

    /**
     * @brief Runs one coordinated global collection pass.
     *
     * Everything else must call this. "force" runs the full sweep
     * immediately (shutdown, /save, tests); otherwise the pass is
     * rate-limited by gc_interval_ticks_ so the world can never be
     * swept mid-tick by several unrelated callers. Runs on the main
     * thread only (all world registries are main-thread or guarded
     * there).
     *
     * @param force  bypass the interval gate but never the
     *               single-entry exclusivity of a pass.
     */
    void collect(bool force = false);

    /**
     * @brief Runs a forced collection pass (legacy alias for collect(true)).
     *
     * Name kept for compatibility with existing call sites.
     */
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
