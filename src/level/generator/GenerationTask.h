// GenerationTask — wraps a single chunk generation request for the
// AsyncPool. Worker thread picks up the GenerationTask, runs
// generator->generate + populate, and posts the chunk back to the
// Level thread via a thread-safe queue.
#pragma once
#include "level/generator/Generator.h"
#include "level/format/Chunk.h"
#include "level/format/ChunkSection.h"
#include <functional>
#include <memory>
#include <string.h>
namespace atomikpe::level::generator {
class GenerationTask {
public:
    using DoneCallback = std::function<void(mcp_chunk_t*)>;
    GenerationTask(std::shared_ptr<Generator> gen, int32_t x, int32_t z, int64_t seed, DoneCallback done)
        : gen_(std::move(gen)), x_(x), z_(z), seed_(seed), done_(std::move(done)) {}
    void run() {
        mcp_chunk_t* c = mcp_chunk_new(x_, z_);
        if (!c) { done_(nullptr); return; }

        // Same defensive defaults ChunkManager::getChunk()'s synchronous
        // path applies before generating: full skylight + plains biome,
        // in case the generator doesn't set its own (Normal/Flat both
        // do, but this keeps behaviour identical for any future one
        // that doesn't). Runs on the worker thread — Generator is
        // documented stateless w.r.t. the main thread, so this is safe.
        for (int s = 0; s < 16; ++s) {
            mcp_chunk_section_t* sec = mcp_chunk_get_or_alloc_section(c, (uint8_t)s);
            if (sec) {
                if (sec->sky_light) memset(sec->sky_light, 0xFF, 2048);
                if (sec->block_light) memset(sec->block_light, 0x00, 2048);
            }
        }
        for (int bx = 0; bx < 16; ++bx)
            for (int bz = 0; bz < 16; ++bz)
                mcp_chunk_set_biome(c, bx, bz, 1);

        GenerationContext ctx{seed_, x_, z_, nullptr, c};
        gen_->generate(ctx);
        gen_->populate(ctx);
        c->terrain_populated = true;
        c->light_populated = true;
        c->dirty = true;
        mcp_chunk_recalculate_height_map(c);
        done_(c);
    }
private:
    std::shared_ptr<Generator> gen_;
    int32_t x_, z_;
    int64_t seed_;
    DoneCallback done_;
};
} // namespace
