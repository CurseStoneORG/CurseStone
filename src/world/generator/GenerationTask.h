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
// GenerationTask — wraps a single chunk generation request for the
// AsyncPool. Worker thread picks up the GenerationTask, runs
// generator->generate + populate, and posts the chunk back to the
// World thread via a thread-safe queue.
#pragma once
#include "world/generator/Generator.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"
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
    void run() {
        // FIX (chunks that never render): everything below runs on an
        // AsyncPool worker. If generate()/populate() throws for some
        // chunk, runOne() swallows the exception but run() never reaches
        // done_(c) — the only place that clears pending_. The chunk would
        // stay "in flight" forever and the world would keep a permanent
        // hole. Fix: catch here, build an emergency flat chunk (same
        // pattern as the synchronous fallback) and guarantee done_() runs
        // either way.
        mcp_chunk_t* partial = nullptr;
        try {
            runInner(partial);
        } catch (const std::exception& e) {
            // 'partial' is the chunk runInner had already allocated
            // before generate()/populate() threw — free it.
            if (partial)
                mcp_chunk_destroy(partial);
            done_(makeEmergencyChunk(e.what()));
        } catch (...) {
            if (partial)
                mcp_chunk_destroy(partial);
            done_(makeEmergencyChunk("unknown exception"));
        }
    }

  private:
    mcp_chunk_t* makeEmergencyChunk(const char* reason) {
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
        (void)reason; // kept for future logging if a logger ever lands here
        return c;
    }
    void runInner(mcp_chunk_t*& partial_out) {
        mcp_chunk_t* c = mcp_chunk_new(x_, z_);
        if (!c) {
            done_(nullptr);
            return;
        }
        partial_out = c; // visible al catch de run() si algo de abajo explota

        // Defensive default biome (plains) in case the generator
        // doesn't set its own (Normal/Flat both do, but this keeps
        // behaviour identical for any future one that doesn't). Runs
        // on the worker thread — Generator is documented stateless
        // w.r.t. the main thread, so this is safe.
        //
        // REAL BUG (black world/grass): this used to fill sky_light with
        // 0xFF before a single block existed and never computed real
        // light — full brightness everywhere plus a broken heightmap that
        // counted leaves/flowers as ceiling, rendering solid black under
        // any tree canopy.
        // Fix: generate terrain first, THEN recalc the heightmap, then
        // run the real light pass (which replaces the old memset).
        for (int bx = 0; bx < 16; ++bx)
            for (int bz = 0; bz < 16; ++bz)
                mcp_chunk_set_biome(c, bx, bz, 1);

        GenerationContext ctx{seed_, x_, z_, nullptr, c};
        gen_->generate(ctx);
        gen_->populate(ctx);
        c->terrain_populated = true;
        c->dirty = true;
        mcp_chunk_recalculate_height_map(c);
        mcp_chunk_propagate_sky_light(c); // sets c->light_populated = true internally
        done_(c);
    }

  private:
    std::shared_ptr<Generator> gen_;
    int32_t x_, z_;
    int64_t seed_;
    DoneCallback done_;
};
} // namespace
