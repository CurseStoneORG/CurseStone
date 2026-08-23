// GenerationTask — wraps a single chunk generation request for the
// AsyncPool. Worker thread picks up the GenerationTask, runs
// generator->generate + populate, and posts the chunk back to the
// Level thread via a thread-safe queue.
#pragma once
#include "level/generator/Generator.h"
#include "level/format/Chunk.h"
#include <functional>
#include <memory>
namespace cenisyspe::level::generator {
class GenerationTask {
public:
    using DoneCallback = std::function<void(mcp_chunk_t*)>;
    GenerationTask(std::shared_ptr<Generator> gen, int32_t x, int32_t z, int64_t seed, DoneCallback done)
        : gen_(std::move(gen)), x_(x), z_(z), seed_(seed), done_(std::move(done)) {}
    void run() {
        mcp_chunk_t* c = mcp_chunk_new(x_, z_);
        if (!c) { done_(nullptr); return; }
        GenerationContext ctx{seed_, x_, z_, nullptr, c};
        gen_->generate(ctx);
        gen_->populate(ctx);
        done_(c);
    }
private:
    std::shared_ptr<Generator> gen_;
    int32_t x_, z_;
    int64_t seed_;
    DoneCallback done_;
};
} // namespace
