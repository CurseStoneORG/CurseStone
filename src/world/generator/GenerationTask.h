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
        // FIX (chunks que nunca se renderizan): TODO el cuerpo de abajo
        // corre en un worker thread de AsyncPool. Si generate()/populate()
        // del generador lanza una excepción en ALGÚN chunk puntual (ej.
        // un populator de árboles/cuevas/minerales que se sale de rango
        // en un borde de chunk), AsyncPool::runOne() atrapa esa excepción
        // para no tumbar el proceso — pero eso corta run() ANTES de
        // llegar a done_(c). Y done_ es el ÚNICO lugar donde
        // ChunkManager::getChunkOrRequestAsync() borra la entrada
        // 'pending_' de ese chunk. Sin ese erase, pending_.count(key)
        // sigue devolviendo true para siempre → ese chunk nunca se
        // vuelve a pedir → agujero permanente en el mundo, solo en los
        // chunks donde el generador falló (por eso "a veces" y no
        // siempre). Fix: atrapar la excepción ACÁ, generar un chunk
        // plano de emergencia (mismo patrón que el fallback síncrono de
        // ChunkManager::getChunk) y garantizar que done_() se llame
        // siempre, éxito o no, para que pending_ se libere y el chunk
        // deje de estar "en vuelo" para siempre.
        mcp_chunk_t* partial = nullptr;
        try {
            runInner(partial);
        } catch (const std::exception& e) {
            // 'partial' es el chunk que runInner ya había allocado antes
            // de que generate()/populate() explotara — liberarlo para no
            // perder esa memoria en cada reintento del chunk.
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
        (void)reason; // motivo queda solo para logging futuro si se agrega logger aquí
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
