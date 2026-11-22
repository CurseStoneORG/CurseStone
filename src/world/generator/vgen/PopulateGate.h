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
