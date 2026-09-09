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

        // Defensive default biome (plains) in case the generator
        // doesn't set its own (Normal/Flat both do, but this keeps
        // behaviour identical for any future one that doesn't). Runs
        // on the worker thread — Generator is documented stateless
        // w.r.t. the main thread, so this is safe.
        //
        // BUG REAL (mundo/pasto negro) — esta función ANTES rellenaba
        // sky_light con 0xFF en las 16 secciones ANTES de que el
        // generador pusiera un solo bloque, y después nunca corría
        // ningún cálculo real de luz — solo marcaba light_populated
        // en true. Eso dejaba luz plena en cada bloque de cada
        // sección sin importar qué tan enterrado estuviera, y el
        // heightmap (además roto — contaba hojas/flores como techo)
        // desalineaba lo que el cliente esperaba ver. Resultado:
        // negro sólido bajo cualquier dosel de árbol.
        // Fix: primero generar terreno, LUEGO recalcular heightmap,
        // y recién ahí correr el pase real de propagación de luz
        // (mcp_chunk_propagate_sky_light), que reemplaza por completo
        // al viejo memset(0xFF).
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
