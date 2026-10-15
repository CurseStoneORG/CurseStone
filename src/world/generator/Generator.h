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
// Generator — abstract world generator interface.
// Mirrors Nukkit's cn.nukkit.level.generator.Generator: a generator is
// asked to populate a chunk's terrain, biomes and features. Generators
// run on AsyncPool workers (one chunk at a time), so implementations must
// be self-contained and stateless w.r.t. the main thread (the seed is
// the only state).
//
// CRITICAL: each generator defines its OWN spawn point via getSpawn().
// This mirrors Nukkit where Normal returns (127.5, 128, 127.5), Flat
// returns (128, floorLevel, 128), and Nether returns (128, 64, 128).
// The World no longer hardcodes a spawn position — it asks the generator.
#pragma once
#include "world/format/Chunk.h"
#include "math/Vector3.h"
#include <stdint.h>

namespace cursestone::world::generator {

class TerrainNoise; // definido en TerrainShape.h

// NOTE: la generacion corre 1 chunk por tarea en el worker del AsyncPool,
// asi que el ctx vive y muere en un solo hilo: un puntero plano sin
// ownership es seguro.
struct GenerationContext {
    int64_t seed; // world seed
    int32_t chunk_x;
    int32_t chunk_z;
    const uint8_t* biomes_in; // optional input biome map (256 bytes), NULL = generate
    mcp_chunk_t* chunk;       // target chunk — generator fills sections + biomes + heightmap

    // OPT (generacion lenta): ruido compartido entre Normal::generate y
    // los populators del MISMO chunk. Es un TerrainNoise construido con
    // ctx.seed y poseido por el que lo crea (Normal::generate, en el
    // mismo scope que ctx). NULL = el populator construye el suyo
    // (comportamiento por defecto; los tools viejos con ctx{} ni se
    // enteran).
    //
    // Por que IMPORTA: construir TerrainNoise cuesta ~17 ms (8 capas de
    // PerlinNoise + la calibracion de 5 campos, 5 x 4096 muestras) y
    // antes CINCO populators lo construian cada uno por chunk: ~85 ms de
    // trabajo identico tirado por chunk.
    void* noise_cache = nullptr; // TerrainNoise* opaco, evita depender del header
};

class Generator {
  public:
    virtual ~Generator() = default;
    virtual const char* name() const = 0;

    // Phase 1: terrain + biomes + heightmap
    virtual void generate(GenerationContext& ctx) = 0;

    // Phase 2: features (trees, ores, grass, flowers, caves) — may
    // touch neighbouring chunks; called after terrain_populated=true.
    virtual void populate(GenerationContext& ctx) = 0;

    // Returns the world spawn position for this generator type.
    // Mirrors Nukkit's Generator.getSpawn() — each generator type
    // defines its own spawn (Normal, Flat, Nether all differ).
    // The World calls this on first load to set the spawn point.
    virtual mcp_vector3_t getSpawn() const = 0;
};

} // namespace
