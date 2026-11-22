/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
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
// PopulateGate — reproduce the vanilla populate order under concurrent
// workers.
//
// Vanilla populates chunks strictly in chunk order, and chunk output is
// NOT order-independent: a populate replays the cross-chunk feature
// writes (spills) that earlier chunks parked, and structure pieces
// mutate during postProcess (lazy ground height, consumed pieces), so
// the windows of one structure depend on which windows ran before them.
// With N pool workers the completion order races, and measured digests
// change run to run (tools/gen_equiv_matrix caught it: 47 of 49 chunks
// differed from the sequential pass, threads=4, every run a different
// digest).
//
// The fix coordinates instead of serializing: every chunk gets a rank —
// its position in the reference order (ascending chunk coords, the same
// order the sequential tools use). A populate may only run once every
// lower-ranked chunk that can still affect it has finished:
//   * the 3x3 neighbourhood — their spills must be parked before this
//     populate replays them, or the replay set depends on timing;
//   * the chunks whose window intersects a structure start this chunk
//     also claims — piece state must move in reference order.
// Chunks outside those sets populate fully in parallel, so terrain
// throughput keeps scaling; only genuinely interacting chunks wait.
//
// tryEnter() is NON-blocking on purpose: it runs inside an AsyncPool
// task, and a worker that blocks on an unfinished dependency can
// deadlock the whole pool (every worker waiting on tasks still sitting
// in the queue). The caller re-queues its task instead and retries —
// dependencies always point down-rank, so the lowest unfinished populate
// is always runnable and progress is guaranteed.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>

namespace vgen {

// Rank in the reference populate order: ascending (cx, cz). The +2^31
// bias keeps negative coords ahead of positives, matching signed order.
inline uint64_t populateRank(int32_t cx, int32_t cz) {
    const uint64_t bx = (uint64_t)((int64_t)cx + 0x80000000LL);
    const uint64_t bz = (uint64_t)((int64_t)cz + 0x80000000LL);
    return (bx << 32) | bz;
}

// ---------------------------------------------------------------------------
// Plains decorator reference state (the vanilla 1-in-10 forest-grass branch).
//
// Vanilla BiomeDecorator::decorate reads Biome::plains->decorator->grassCount
// for its 1-in-10 "forest" branch, and PlainsBiome::decorate retunes that
// same shared decorator on every plains populate. The value the branch sees
// is therefore plains' decorator state carried over from earlier populations
// — a strictly-sequential-populate concept that concurrent workers can
// neither share safely (the pre-fix singleton raced, digest e21c87 -> race)
// nor simply freeze (the constructor default diverges from the sequential
// reference: seed 1337 radius 5 shifted one brown-mushroom cell because the
// branch read 10 instead of the 5 a plains chunk two ranks earlier had
// retuned).
//
// The reference order makes the carry-over reproducible WITHOUT shared
// mutable state: under the gate's rank discipline, plains' decorator state
// at the moment rank K populates is exactly the retune of the highest plains
// rank <= K in the registered chunk set — a plains chunk reads its own just
// retuned value, a non-plains chunk reads the nearest lower-ranked plains
// chunk's. Each retune is a pure function of the chunk coordinates
// (seed-independent biome-info noise), so the table is fully known at
// register time, before any populate runs, and every read returns the same
// value on every worker and every run. The table lives per VgenSource; a
// thread-local pointer carries it into BiomeDecorator::decorate for the
// duration of one populate.
class PlainsRefTable {
public:
    void note(uint64_t rank, int grass) {
        std::lock_guard<std::mutex> lk(mtx_);
        plainsByRank_[rank] = grass;
    }

    // The decorator state rank K's 1-in-10 branch would read under the
    // reference order; the PlainsBiome constructor default when no plains
    // chunk precedes it (vanilla: nothing populated plains' decorator yet).
    int grassAtOrBefore(uint64_t rank) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = plainsByRank_.upper_bound(rank);
        if (it == plainsByRank_.begin())
            return kDefaultGrassCount;
        --it;
        return it->second;
    }

    static constexpr int kDefaultGrassCount = 10; // PlainsBiome ctor value

private:
    mutable std::mutex mtx_;
    std::map<uint64_t, int> plainsByRank_;
};

// Installed by VgenSource::populate for the duration of one populate. Reads
// outside a populate (tools, tests) fall back to the constructor default —
// the vanilla singleton's never-populated state.
inline PlainsRefTable*& currentPlainsRef() {
    static thread_local PlainsRefTable* t = nullptr;
    return t;
}

// RAII installer — populate has early returns, so the restore must not
// depend on reaching the end of the function.
class PlainsRefGuard {
public:
    explicit PlainsRefGuard(PlainsRefTable& table)
        : saved_(currentPlainsRef()) {
        currentPlainsRef() = &table;
    }
    ~PlainsRefGuard() { currentPlainsRef() = saved_; }
    PlainsRefGuard(const PlainsRefGuard&) = delete;
    PlainsRefGuard& operator=(const PlainsRefGuard&) = delete;

private:
    PlainsRefTable* saved_;
};

class PopulateGate {
public:
    // Claim a slot for a chunk that was requested and will populate.
    // Idempotent; called when the request is accepted, before any task
    // runs, so a dependency that is queued but not started is still
    // visible as "not done" to everyone.
    void registerSlot(uint64_t key) {
        std::lock_guard<std::mutex> lk(mtx_);
        slots_.emplace(key, Pending);
    }

    // Non-blocking: returns true only when the slot is pending AND every
    // dependency in `deps` is done (absent from the map = never
    // requested = can never write = done). On success the slot moves to
    // Running; the caller MUST eventually markDone() it, or waiters
    // would spin forever.
    template <class DepRange>
    bool tryEnter(uint64_t key, const DepRange& deps) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = slots_.find(key);
        if (it == slots_.end() || it->second != Pending)
            return false;
        for (const uint64_t d : deps) {
            auto dit = slots_.find(d);
            if (dit != slots_.end() && dit->second != Done)
                return false;
        }
        it->second = Running;
        return true;
    }

    // Release the slot (erase + wake re-queuers). Absent keys are fine.
    void markDone(uint64_t key) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            slots_.erase(key);
        }
        cv_.notify_all();
    }

    // Debug/telemetry helper: -1 = absent (done or never registered),
    // 0 = pending, 1 = running.
    int stateOf(uint64_t key) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = slots_.find(key);
        if (it == slots_.end())
            return -1;
        return (int)it->second;
    }

    size_t size() {
        std::lock_guard<std::mutex> lk(mtx_);
        return slots_.size();
    }

private:
    enum State : uint8_t { Pending, Running, Done };
    std::mutex mtx_;
    std::condition_variable cv_; // kept for future blocking waiters
    std::unordered_map<uint64_t, State> slots_;
};

} // namespace vgen
