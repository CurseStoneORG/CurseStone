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
// SPDX-License-Identifier: LGPL-3.0-or-later
// LightEngine — sky and block light that actually behaves like vanilla.
//
// The engine has two kinds of work, and telling them apart is what
// keeps flight TPS stable:
//
//   * FULL sweep (relightChunk): clear a chunk PLUS one light range
//     into every neighbour and re-flood the whole box from scratch.
//     Exact (it re-derives the vanilla fixed point for the box) but
//     expensive — hundreds of thousands of cells. Reserved for chunks
//     whose BLOCKS changed: World::setBlock and generation spill
//     batches. Darkening (a roof going up) needs the clear, so only a
//     full sweep can do it.
//
//   * BORDER entry (runBorderEntry): everything else — a chunk that
//     just arrived with its interior already lit by the generation
//     worker. Three cheap monotone steps: borderPull raises the
//     chunk's cells from its neighbours' current light, pushBorder
//     raises neighbour cells the chunk can improve, and interiorFlood
//     spreads light a neighbour deposited mid-chunk (flagged via
//     light_touched). Values only ever RISE and are capped at 15, so
//     the pass converges to the same fixed point the full sweep
//     reaches, without ever re-wiping a healthy neighbour.
//
// A wall-clock budget in processPending keeps the main-thread tick
// flat no matter how fast chunks arrive; flushPending finalizes one
// chunk's queued entry on demand (the chunk-send path uses it so a
// protocol-84 client never receives a chunk whose border pass has not
// run yet — clients keep the light bytes they get).
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cursestone::world {

class World;

class LightEngine {
  public:
    // Light falls off after 15 steps, so a region 15 blocks wider than the
    // change on every side can never miss a source further away: the
    // recomputation is self-contained.
    static constexpr int32_t kRelightRadius = 15;
    // A chunk whose blocks changed needs its neighbours relit too, so
    // the sweep extends one full light range past the chunk border.
    static constexpr int32_t kChunkRelightRadius = 15;

    // Chunks relit per server tick at most, so loading a view full of
    // chunks never stalls the main loop in one giant burst.
    static constexpr int32_t kPendingBudget = 2;
    // Wall-clock cap for the per-tick light drain (processPending stops
    // before STARTING another entry once this many ms are spent; an
    // entry that already started always completes).
    static constexpr int32_t kTickBudgetMs = 6;

    explicit LightEngine(World& lvl) : lvl_(lvl) {
    }

    // Recompute both layers in a cube around a block change. The home
    // chunk is (x >> 4, z >> 4); every other chunk the sweep writes is
    // flagged (light_touched) and re-queued for a border entry, so
    // light the sweep deposited in a neighbour never freezes.
    void relightAround(int32_t x, int32_t y, int32_t z, int32_t radius);

    // Same sweep, centred on a chunk, used when a chunk's blocks
    // changed (generation spill batch) so the rewrite covers one full
    // light range into every neighbour.
    void relightChunk(int32_t cx, int32_t cz, int32_t radius);

    // Called from the chunk worker thread when a chunk enters the
    // manager. Only queues the chunk — doing real work here would block
    // generation and risk lock inversion with ChunkManager::mtx_.
    // `full` selects the entry kind: true = blocks changed under this
    // chunk (spill batch / the tools' re-signal), false = the chunk
    // merely arrived with worker-lit interior (a BORDER entry).
    void onChunkAdded(int32_t cx, int32_t cz, bool full = true);

    // Drain up to `budget` queued chunks, stopping before starting
    // another entry once `max_ms` (if > 0) is spent. Returns how many
    // entries were completed.
    size_t processPending(int32_t budget, int32_t max_ms = 0);

    // Run the queued entry for exactly this chunk right now (removing
    // it from the queue). Returns false when nothing is queued for it.
    // The chunk-send path calls this so a chunk never leaves with its
    // border pass still pending.
    bool flushPending(int32_t cx, int32_t cz);

    bool hasPending() const;

    // One exact clearing re-flood over the whole box: clear + reseed +
    // flood, no queueing, no home chunk, no neighbour re-queues. This is
    // the ground-truth fixed point the convergence judge
    // (tools/light_oracle.cpp) compares the FULL and BORDER drains
    // against.
    void relightBoxExact(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                         int32_t z1) {
        relight(x0, y0, z0, x1, y1, z1, INT32_MIN, 0);
    }

  private:
    struct PendingEntry {
        int32_t cx, cz;
        bool full;
    };

    World& lvl_;
    mutable std::mutex mtx_;
    std::deque<PendingEntry> pending_;
    // Same members as pending_, as a set: population storms used to call
    // onChunkAdded for the same chunk once per spilled block, and the
    // duplicate check scanned the whole deque each time (O(n) per block).
    // A queued Border entry is UPGRADED to Full when a spilled-block
    // batch lands on the same chunk before its turn.
    std::unordered_set<uint64_t> pending_keys_;

    // Runs one full region recomputation for both layers. When
    // (homeCx, homeCz) is a real chunk, every OTHER loaded chunk the
    // box touches is flagged light_touched and re-queued as a border
    // entry; pass homeCx = INT32_MIN to disable (the oracle's exact
    // global re-flood).
    void relight(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                 int32_t homeCx = INT32_MIN, int32_t homeCz = 0);

    // One BORDER entry: pull neighbours' light in, spread mid-chunk
    // deposits, push our light out, re-queue neighbours whose cells the
    // push raised.
    void runBorderEntry(int32_t cx, int32_t cz);
    void borderPull(int32_t cx, int32_t cz);
    void pushBorder(int32_t cx, int32_t cz, std::vector<std::pair<int32_t, int32_t>>* touched);
    void interiorFlood(int32_t cx, int32_t cz);

    // Queue helper shared by runBorderEntry / relight's re-queue.
    void queueEntry(int32_t cx, int32_t cz, bool full);
};

} // namespace cursestone::world