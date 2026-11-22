/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
// GenerationTask — wraps a single chunk generation request for the
// AsyncPool. Worker thread picks up the GenerationTask, runs
// generator->generate + populate, and posts the chunk back to the
// World thread via a thread-safe queue.
#pragma once
#include "world/generator/Generator.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"
#include "world/performance/GenerationProfiler.h"
#include <functional>
#include <memory>
#include <string.h>
namespace cursestone::world::generator {
class GenerationTask {
  public:
    using DoneCallback = std::function<void(mcp_chunk_t*)>;
    GenerationTask(std::shared_ptr<Generator> gen, int32_t x, int32_t z, int64_t seed,
                   DoneCallback done)
        : gen_(std::move(gen)), x_(x), z_(z), seed_(seed), done_(std::move(done)) {
    }

    // Combined path (legacy callers): generate then populate, exactly the
    // old single-task behaviour.
    void run() {
        mcp_chunk_t* c = runGenerate();
        if (c)
            runPopulate(c);
    }

    // Phase 1 of the split pipeline: create the chunk and run
    // generator->generate() on this worker. On success returns the chunk
    // for phase 2 (runPopulate). If generate() throws, the emergency flat
    // chunk is handed to done_() and null returns (phase 2 skipped); an
    // allocation failure hands done_(nullptr) and returns null. The gate
    // (populateTryEnter) is NOT touched here — phase 1 is order-free.
    mcp_chunk_t* runGenerate() {
        const auto t0 = std::chrono::steady_clock::now();
        mcp_chunk_t* partial = nullptr;
        try {
            mcp_chunk_t* c = mcp_chunk_new(x_, z_);
            if (!c) {
                done_(nullptr);
                return nullptr;
            }
            partial = c; // visible to the catch below if generate() throws

            // Defensive default biome (plains) in case the generator
            // doesn't set its own (Normal/Flat both do, but this keeps
            // behaviour identical for any future one that doesn't).
            //
            // REAL BUG (black world/grass): this used to fill sky_light
            // with 0xFF before a single block existed and never computed
            // real light — full brightness everywhere plus a broken
            // heightmap that counted leaves/flowers as ceiling, rendering
            // solid black under any tree canopy.
            // Fix: generate terrain first, THEN recalc the heightmap, then
            // run the real light pass (phase 2), never a memset.
            for (int bx = 0; bx < 16; ++bx)
                for (int bz = 0; bz < 16; ++bz)
                    mcp_chunk_set_biome(c, bx, bz, 1);

            GenerationContext ctx{seed_, x_, z_, nullptr, c};
            gen_->generate(ctx);
            generate_ns_ = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
            return c;
        } catch (const std::exception& e) {
            if (partial)
                mcp_chunk_destroy(partial);
            done_(makeEmergencyChunk());
            return nullptr;
        } catch (...) {
            if (partial)
                mcp_chunk_destroy(partial);
            done_(makeEmergencyChunk());
            return nullptr;
        }
    }

    // Phase 2: the populate-gate turn must already be held (the caller
    // asked populateTryEnter first). Runs populate, flags, heightmap and
    // light, then hands the chunk to done_() exactly once — an emergency
    // flat chunk if populate threw, same guarantee as the old combined
    // run(). One Stage::Total entry per chunk: generate time is carried
    // over from phase 1 so the per-chunk number stays whole.
    void runPopulate(mcp_chunk_t* c) {
        if (!c)
            return; // phase 1 already handed the task off via done_
        const auto t0 = std::chrono::steady_clock::now();
        try {
            GenerationContext ctx{seed_, x_, z_, nullptr, c};
            gen_->populate(ctx);
            c->terrain_populated = true;
            c->dirty = true;
            mcp_chunk_recalculate_height_map(c);
            {
                performance::ScopedStage lightTimer(performance::Stage::Light);
                mcp_chunk_propagate_sky_light(c); // sets c->light_populated = true internally
            }
            done_(c);
        } catch (...) {
            mcp_chunk_destroy(c);
            done_(makeEmergencyChunk());
        }
        performance::GenerationProfiler::add(
            performance::Stage::Total,
            generate_ns_ +
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count());
    }

  private:
    mcp_chunk_t* makeEmergencyChunk() {
        mcp_chunk_t* c = mcp_chunk_new(x_, z_);
        if (!c)
            return nullptr;
        for (int bx = 0; bx < 16; ++bx) {
            for (int bz = 0; bz < 16; ++bz) {
                mcp_chunk_set_block_id(c, bx, 0, bz, 7); // BEDROCK
                mcp_chunk_set_block_id(c, bx, 1, bz, 3); // DIRT
                mcp_chunk_set_block_id(c, bx, 2, bz, 3); // DIRT
                mcp_chunk_set_block_id(c, bx, 3, bz, 2); // GRASS
                mcp_chunk_set_biome(c, bx, bz, 1);
            }
        }
        c->terrain_populated = true;
        c->dirty = true;
        mcp_chunk_recalculate_height_map(c);
        mcp_chunk_propagate_sky_light(c);
        return c;
    }

    uint64_t generate_ns_ = 0;

  private:
    std::shared_ptr<Generator> gen_;
    int32_t x_, z_;
    int64_t seed_;
    DoneCallback done_;
};
} // namespace
