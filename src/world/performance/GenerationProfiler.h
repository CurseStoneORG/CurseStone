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
// GenerationProfiler — where generation time actually goes.
//
// Every stage of the chunk pipeline (terrain noise, caves, populate,
// light, wire encode, disk load) accumulates nanoseconds and call
// counts into lock-free counters; PerformanceManager formats a
// per-chunk breakdown out of them. The overhead is two steady_clock
// reads plus two atomic adds per scoped stage — noise next to the
// milliseconds of real work a chunk costs.
//
// gen.noise, gen.surface and gen.caves are measured INSIDE
// gen.terrain; gen.copy_in/gen.copy_out run inside gen.populate. The
// numbers overlap on purpose: read them as "what does each part cost",
// not as a partition of gen.total.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace cursestone::performance {

enum class Stage : uint8_t {
    Total,        // whole chunk task: terrain + populate + light
    TerrainBuild, // terrain-only chunk build (prepareHeights..carve)
    Noise,        // Perlin octave regions inside prepareHeights
    Surface,      // biome surface patches
    Caves,        // LargeCaveFeature carving
    BiomeFill,    // biome layer stack sampling
    Populate,     // features, ores, structures, snow
    CopyIn,       // real chunk -> virtual working copy
    CopyOut,      // virtual volume -> real chunk sections
    Light,        // sky light propagation
    Encode,       // chunk wire transposition + zlib (worker)
    Snapshot,     // chunk wire snapshot (tick thread)
    DiskLoad,     // provider loadChunk (region read + inflate + NBT)
    Lakes,        // water + lava lake placement
    Structures,   // village / mineshaft / scattered temple passes
    Dungeons,     // monster rooms
    Freeze,       // freeze tops scan
    Decorate,     // biome decoration umbrella
    DecorateOres, // ore vein placement
    DecorateSurface, // sand / clay / gravel / mushroom blobs
    DecorateTrees,   // trees + tall grass around them
    DecorateMisc,    // flowers, tall grass, sugar cane, pumpkins...
    Snow,         // snow cover pass
    Count
};

class GenerationProfiler {
  public:
    struct Entry {
        uint64_t ns = 0;
        uint64_t calls = 0;
    };
    struct Snapshot {
        Entry stages[(size_t)Stage::Count];
    };

    // Timers are on by default; the cost is a few atomic adds per chunk.
    static bool enabled() {
        return enabled_.load(std::memory_order_relaxed);
    }
    static void setEnabled(bool on) {
        enabled_.store(on, std::memory_order_relaxed);
    }

    static void add(Stage s, uint64_t ns) {
        if (!enabled())
            return;
        auto& e = stages_[(size_t)s];
        e.ns.fetch_add(ns, std::memory_order_relaxed);
        e.calls.fetch_add(1, std::memory_order_relaxed);
    }
    // Atomically drain the counters: concurrent stages keep adding into
    // the fresh counters, so a snapshot is "at least everything up to
    // now" rather than an exact instant — fine for periodic reporting.
    static Snapshot drain() {
        Snapshot out;
        for (size_t i = 0; i < (size_t)Stage::Count; ++i) {
            out.stages[i].ns = stages_[i].ns.exchange(0, std::memory_order_relaxed);
            out.stages[i].calls =
                stages_[i].calls.exchange(0, std::memory_order_relaxed);
        }
        return out;
    }
    // Non-destructive read for reports that drain elsewhere (or diff two
    // reads to describe a window without consuming anything).
    static Snapshot peek() {
        Snapshot out;
        for (size_t i = 0; i < (size_t)Stage::Count; ++i) {
            out.stages[i].ns = stages_[i].ns.load(std::memory_order_relaxed);
            out.stages[i].calls =
                stages_[i].calls.load(std::memory_order_relaxed);
        }
        return out;
    }

  private:
    struct AtomicEntry {
        // Zero-initialized by static storage; no NSDMI here so the
        // stages_ array can be declared inside the class body.
        std::atomic<uint64_t> ns;
        std::atomic<uint64_t> calls;
    };
    static inline std::atomic<bool> enabled_{true};
    static inline AtomicEntry stages_[(size_t)Stage::Count];
};

// Times one scope into one stage. Cheap enough to sprinkle freely.
class ScopedStage {
  public:
    explicit ScopedStage(Stage s)
        : stage_(s), start_(GenerationProfiler::enabled()
                               ? std::chrono::steady_clock::now()
                               : TimePoint{}) {}
    ~ScopedStage() {
        if (!GenerationProfiler::enabled())
            return;
        const auto end = std::chrono::steady_clock::now();
        GenerationProfiler::add(stage_,
                                (uint64_t)std::chrono::duration_cast<
                                    std::chrono::nanoseconds>(end - start_)
                                    .count());
    }

  private:
    using TimePoint = std::chrono::steady_clock::time_point;
    Stage stage_;
    TimePoint start_;
};

} // namespace cursestone::performance
