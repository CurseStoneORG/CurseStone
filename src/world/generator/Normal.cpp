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
#include "Normal.h"
#include "world/format/Chunk.h"
#include "world/generator/populator/PopulatorOre.h"
#include "world/generator/populator/PopulatorTree.h"
#include "world/generator/populator/PopulatorTallGrass.h"
#include "world/generator/populator/PopulatorFlower.h"
#include "world/generator/populator/PopulatorCave.h"
#include "world/generator/populator/PopulatorFeatures.h"
#include "world/generator/populator/StructurePopulator.h"
#include "world/generator/populator/VillagePopulator.h"
#include "world/generator/structure/DesertTemple.h"
#include "world/generator/object/OreGenerator.h"
#include "world/generator/biome/Biome.h"
#include "world/generator/biome/BiomeRegistry.h"
#include "block/BlockIds.h"
#include "world/format/ChunkSection.h"
#include <algorithm>
#include <cmath>
#include <memory>
namespace cursestone::world::generator {

using namespace biome;
using object::OreVein;

// ---- Fast path de escritura de columnas (OPT, chunks lentos) --------------
// El bucle interior del relleno escribe de 60 a 120 bloques por columna a
// traves de mcp_chunk_set_block_id(), que POR CADA BLOQUE repite: chequeo
// de rango, busqueda de la seccion (y su alloc perezoso), update de
// height_map y el flag dirty. Con el layout conocido de las secciones
// (array XZY de 4096) y el rango fijado por el propio bucle (x/z en
// [0,15], y en [0,127]) la escritura directa resuelve la seccion UNA vez
// por sub-seccion en vez de por bloque y deja height_map / non_air_top /
// dirty invariantes los mismos una vez por columna. El chunk final es
// identico bit a bit (verificado con el golden dump 7x7 seed 1337).
namespace {
// Resuelve (allocando si falta) la seccion sy del chunk y la devuelve.
static inline mcp_chunk_section_t* section8(mcp_chunk_t* c, int sy) {
    mcp_chunk_section_t* s = c->sections[sy];
    if (!s) { s = mcp_section_new((uint8_t)sy); c->sections[sy] = s; }
    return s;
}
} // namespace

Normal::Normal(int64_t seed) : seed_(seed), noise_(seed) {
    // Vanilla ore distribution (ported from the reference's OrePopulator).
    std::vector<OreVein> ores = {
        {3,  1,   0, 128, 32, 10},  // dirt patches
        {13, 1,   0, 128, 32,  8},  // gravel patches
        {16, 1,   0, 128, 16, 20},  // coal
        {15, 1,   0,  64,  8, 20},  // iron
        {14, 1,   0,  32,  8,  2},  // gold
        {73, 1,   0,  16,  7,  8},  // redstone
        {56, 1,   0,  16,  7,  1},  // diamond
        {21, 1,  16,  24,  6,  1},  // lapis lazuli
    };

    // Populator order matters: ores + caves first (they edit raw ground),
    // then vegetation on the final surface. Feature populators all use 16
    // candidate slots per chunk; the actual density comes from each biome's
    // BiomeConfig amounts, so their ctor argument only sets the slot count.
    //
    // NOTE: the water/lava lake populators that used to sit between caves
    // and trees are gone on purpose — the user reported small pools
    // ("charcos") appearing all over the surface. Rivers are now part of
    // the terrain shape itself (see TerrainShape), so no lake pass is
    // needed.
    populators_.push_back(std::make_unique<populator::PopulatorOre>(std::move(ores)));
    populators_.push_back(std::make_unique<populator::PopulatorCave>(16));

    // Very rare lava pools on the flat plains. It runs after the caves and
    // before the vegetation on purpose: the pools are then part of the raw
    // ground, so no tree or flower gets planted over a fresh one.
    populators_.push_back(std::make_unique<populator::PopulatorLavaPool>(1));

    populators_.push_back(std::make_unique<populator::PopulatorTree>(16));
    populators_.push_back(std::make_unique<populator::PopulatorTallGrass>(16));
    populators_.push_back(std::make_unique<populator::PopulatorDoublePlant>(16, 2)); // large grass
    populators_.push_back(std::make_unique<populator::PopulatorFlower>(37, 16)); // dandelion
    populators_.push_back(std::make_unique<populator::PopulatorFlower>(38, 16)); // poppy
    populators_.push_back(std::make_unique<populator::PopulatorCactus>(16));
    populators_.push_back(std::make_unique<populator::PopulatorDeadBush>(16));
    populators_.push_back(std::make_unique<populator::PopulatorSugarCane>(16));
    populators_.push_back(std::make_unique<populator::PopulatorMushroom>(16, 39)); // brown
    populators_.push_back(std::make_unique<populator::PopulatorMushroom>(16, 40)); // red
    populators_.push_back(std::make_unique<populator::PopulatorPumpkin>(16));
    populators_.push_back(std::make_unique<populator::PopulatorMelon>(16));
    populators_.push_back(std::make_unique<populator::PopulatorWaterLily>(16));

    // Structures run LAST, the way vanilla orders them. A temple has to
    // overwrite the vegetation that the passes above scattered over its
    // footprint — and it is its own volume's air cells that carve out the
    // interior, so it cannot come before the vegetation and be buried
    // under a cactus.
    //
    // Villages sit right before the fixed templates: their roads and
    // house pieces stamp their own air over the vegetation too, and if a
    // desert temple ever shares an anchor with a village (different
    // salts, so this is rare) the temple wins.
    populators_.push_back(std::make_unique<populator::VillagePopulator>());
    auto structures = std::make_unique<populator::StructurePopulator>();
    structures->add(std::make_shared<structure::DesertTemple>());
    populators_.push_back(std::move(structures));
}

Normal::~Normal() = default;

void Normal::generate(GenerationContext& ctx) {
    using cursestone::block::BlockId;
    mcp_chunk_t* c = ctx.chunk;
    const int baseX = ctx.chunk_x * 16;
    const int baseZ = ctx.chunk_z * 16;

    // OPT (chunks lentos): publica el TerrainNoise del generador (ya
    // construido en el ctor con seed_) en ctx.noise_cache. Los populators
    // (árboles, lava pools, pueblo, estructuras) antes construían el suyo
    // propio cada uno: 5 x ~17 ms por chunk de trabajo idéntico. Solo si
    // alguien llama a generate() con una semilla distinta a la del ctor se
    // construye uno provisional (no ocurre en el servidor: World pasa el
    // seed del provider, que es el mismo con el que se construyó el
    // generador). Con el ctor y el ctx ya alineados (ver World::World()),
    // esta rama no se toma en el servidor.
    if (ctx.seed == seed_) {
        ctx.noise_cache = &noise_;
    } else {
        // TerrainNoise provisional para una semilla que el ctor no vio.
        // Tiene que vivir hasta que termina populate() (los populators leen
        // ctx.noise_cache), asi que no puede ser una local de generate().
        // Tampoco puede ser un MIEMBRO: generate()/populate() corren a la vez
        // en varios workers del AsyncPool sobre este MISMO Generator, y un
        // miembro compartido se reconstruye (libera) debajo de los pies del
        // otro worker — ese era justamente el SIGSEGV en
        // PerlinNoise::getValue (el worker A liberaba las capas que el
        // worker B estaba evaluando). Una instancia POR HILO no se comparte
        // nunca, y se reusa entre los chunks que ese hilo genera.
        static thread_local std::unique_ptr<TerrainNoise> tlsNoise;
        tlsNoise = std::make_unique<TerrainNoise>(ctx.seed);
        ctx.noise_cache = tlsNoise.get();
    }
    const TerrainNoise& noise = *static_cast<TerrainNoise*>(ctx.noise_cache);

    // ---- Height field ----------------------------------------------------
    // Raw column heights are a pure function of world coordinates, computed
    // on an 18x18 grid (1-block margin) so the smoothing kernel and the
    // beach/water neighbour test are identical at every chunk border. Two
    // neighbouring chunks therefore agree on every shared column.
    constexpr int GW = 18;   // world offsets -1 .. 16
    double raw[GW][GW];
    for (int i = 0; i < GW; ++i)
        for (int j = 0; j < GW; ++j)
            raw[i][j] = rawColumnHeight(noise, baseX - 1 + i, baseZ - 1 + j);

    int heights[16][16];
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            double k[3][3];
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    k[a][b] = raw[x + a][z + b];
            heights[x][z] = heightFromRaw(k);
        }
    }

    // ---- Biomes ----------------------------------------------------------
    // columnBiome() is the same pure function the tree populator uses, so a
    // tree's species is decided identically by whichever chunk renders it.
    uint8_t biomes[16][16];
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            biomes[x][z] = columnBiome(noise, baseX + x, baseZ + z, heights[x][z]);

    // ---- Fill columns ----------------------------------------------------
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int h = heights[x][z];
            uint8_t b = biomes[x][z];
            const int wx = baseX + x;
            const int wz = baseZ + z;
            const BiomeConfig& cfg = BiomeRegistry::forId(b);

            uint8_t top    = cfg.topBlock;
            uint8_t filler = cfg.fillerBlock;

            // Vanilla-ish surface extras per biome.
            if (b == BIOME_OCEAN) {
                // Deep floor: gravel. Shallow shelf near the shore: sand on
                // top with gravel underneath. surfaceDepth is raised below so
                // the bottom of an ocean is always 3-4 layers of gravel the
                // way vanilla builds an ocean floor.
                top = (h > 60) ? (uint8_t)BlockId::SAND : (uint8_t)BlockId::GRAVEL;
                filler = (uint8_t)BlockId::GRAVEL;
            } else if (b == BIOME_RIVER || b == BIOME_FROZEN_RIVER) {
                // Erosion deposits, the way a real river sorts the ground
                // it works on: the fast water in the deep channel scours the
                // bed down to GRAVEL, while the slow water at the shallow lip
                // drops SAND. Above the waterline the bank is grass and dirt
                // like the land around it.
                if (h < kSeaLevel) {
                    if (h <= kSeaLevel - 3) {      // scoured channel floor
                        top = (uint8_t)BlockId::GRAVEL;
                        filler = (uint8_t)BlockId::GRAVEL;
                    } else {                        // shallow lip / margin
                        top = (uint8_t)BlockId::SAND;
                        filler = (uint8_t)BlockId::SAND;
                    }
                } else {
                    // The BANK is grass, not gravel: a vanilla river has a
                    // green bank 2-3 blocks wide with a single sand block
                    // right at the water. The sand block is applied below
                    // for every land column beside the water, river or not,
                    // so the strip stays continuous.
                    top = (uint8_t)BlockId::GRASS;
                    filler = (uint8_t)BlockId::DIRT;
                }
            } else if (b == BIOME_EXTREME_HILLS) {
                // Rocky tops above the tree line, grass/dirt below. The
                // threshold used to be y=86, which painted a huge share of
                // the hills — including the shoulders a river or a carver
                // cuts through — as solid grey stone (measured up to 11% of
                // the whole surface on some seeds). The player asked for
                // green hills, so rock is now reserved for the genuine high
                // peaks only.
                if (h >= 100) { top = (uint8_t)BlockId::STONE; filler = (uint8_t)BlockId::STONE; }
            }

            // ---- Shorelines: sand at the water line -------------------------
            // Sand belongs along the OCEAN edge, at roughly the height of
            // the water — not only on the submerged block itself.
            // isOceanShore() is the same predicate that picks BIOME_BEACH,
            // so the beach biome and the sand agree, and it deliberately
            // ignores river channels: river banks are green.
            const bool shore = (b != BIOME_OCEAN) && isOceanShore(noise, wx, wz, h);
            // Does this land column touch water on any side? The raw grid
            // has a one-block margin all round (GW = 18, world offsets
            // -1..16), so all four neighbours are available even on the
            // chunk border: column x sits at raw[x + 1].
            const bool touchesWater =
                raw[x][z + 1]     < (double)kSeaLevel - 0.5 ||
                raw[x + 2][z + 1] < (double)kSeaLevel - 0.5 ||
                raw[x + 1][z]     < (double)kSeaLevel - 0.5 ||
                raw[x + 1][z + 2] < (double)kSeaLevel - 0.5;
            if (shore && top != (uint8_t)BlockId::STONE) {
                top = (uint8_t)BlockId::SAND;
                // Sand on top of dirt, the way a beach is built up.
                if (filler == (uint8_t)BlockId::GRASS || filler == (uint8_t)BlockId::DIRT)
                    filler = (uint8_t)BlockId::SAND;
            }

            // ---- River bank: grass, with one sand block at the water -------
            // The reference rivers are ringed by a flat GRASS bank 2-3
            // blocks wide, and exactly one sand block sits on the
            // innermost column, practically touching the water. The rest of
            // the bank keeps whatever the biome put there, which in plains,
            // savanna and forest country is grass.
            //
            // "Innermost column" is tested directly — it is the land column
            // that touches water — rather than by a distance threshold. The
            // river profile is smoothed by the height kernel, so the block
            // distance at which the water actually stops drifts by one or
            // two columns depending on the terrain, and any fixed
            // threshold put the sand either underwater or one block too far
            // up the bank. The neighbour test is exact everywhere, and it
            // automatically covers every land biome the bank runs through.
            if (!shore && h >= kSeaLevel && b != BIOME_OCEAN &&
                top != (uint8_t)BlockId::STONE && touchesWater) {
                top = (uint8_t)BlockId::SAND;
                if (filler == (uint8_t)BlockId::GRASS || filler == (uint8_t)BlockId::DIRT)
                    filler = (uint8_t)BlockId::SAND;
            }

            // ---- Rare but long sand banks along rivers --------------------
            // Every so often a river does run past a proper sandy stretch.
            // The mask is low-frequency on purpose, so when it fires it
            // follows the river for hundreds of blocks rather than making
            // round patches — and it stays rare because it is gated on the
            // river valley AND thresholded hard.
            if (!shore && h >= kSeaLevel &&
                riverSandBank(noise, wx, wz) > 0.62) {
                top = (uint8_t)BlockId::SAND;
                filler = (uint8_t)BlockId::SAND;
            }

            // ---- Rare gravel patches ---------------------------------------
            // Occasional gravel on the ground near rivers, coasts and inland
            // patches, the way vanilla scatters gravel around.
            if (!shore && h >= kSeaLevel && top != (uint8_t)BlockId::STONE &&
                gravelPatch(noise, wx, wz, h) > 0.80) {
                top = (uint8_t)BlockId::GRAVEL;
            }

            // ---- Steep ground stays green ----------------------------------
            // This used to strip the grass off any slope whose neighbouring
            // column was 4+ blocks different and put bare STONE on top
            // instead. On the naturally craggy terrain that rule fired
            // constantly, so every river-cut canyon, every savanna canyon
            // wall and most hill shoulders came out as a checkerboard of
            // grey stone sitting on top of the grass — exactly the "stone
            // canyon" the player kept seeing. The player asked for those
            // walls to be grass, so the surface now keeps whatever the
            // biome put there (grass/dirt/sand). Rock is still visible on
            // the vertical face of a cliff, because the fill below the top
            // layers is stone — that is how a cut bank reads naturally —
            // but the ground itself is no longer painted grey.

            const bool snowy = cfg.snowy || b == BIOME_FROZEN_RIVER;

            // How many layers (top + filler) the biome's soil occupies
            // before the solid stone begins.
            //
            // This is deliberately SHORT. The reference river banks are
            // terraced, and every step shows a green grass top, a thin
            // dirt band and then the grey stone of the hillside underneath
            // — those horizontal dirt/stone bands are the whole look of a
            // river cut, and they only appear if the soil layer stays thin.
            // A previous pass stretched the soil over the full height of a
            // cliff so no bare stone would ever show; it turned every river
            // bank into a featureless wall of dirt and threw those bands
            // away. The extension is now only HALF the drop and capped at
            // 8, which keeps a sheer wall from becoming one flat slab while
            // still leaving the layering visible on every terrace.
            int surfaceDepth = 4;
            int lowN = h;
            if (x > 0)  lowN = std::min(lowN, heights[x - 1][z]);
            if (x < 15) lowN = std::min(lowN, heights[x + 1][z]);
            if (z > 0)  lowN = std::min(lowN, heights[x][z - 1]);
            if (z < 15) lowN = std::min(lowN, heights[x][z + 1]);
            if (b != BIOME_EXTREME_HILLS && b != BIOME_MOUNTAINS)
                surfaceDepth = std::min(4 + (h - lowN) / 2, 8);

            // ---- Eroded gorge: a rock cliff, not a dirt bank ---------------
            // Where the ground stands several blocks above its neighbour the
            // exposed face is a cut rock wall, and that is what a river gorge
            // through high country looks like: a green lip on top and then
            // bare stone, showing the hillside's own layers, exactly like the
            // mountain faces. Only applied inside the river's own cut, so the
            // terraced lowland banks and the savanna plateau walls keep the
            // soil the player asked for there.
            if (b != BIOME_OCEAN && b != BIOME_RIVER && b != BIOME_FROZEN_RIVER &&
                h - lowN >= 5 && riverDistance(noise, wx, wz) < 26.0)
                filler = (uint8_t)BlockId::STONE;
            // Oceans and submerged river beds keep 4 gravel layers at the
            // bottom regardless of the surrounding relief.
            if (b == BIOME_OCEAN || b == BIOME_RIVER || b == BIOME_FROZEN_RIVER)
                surfaceDepth = std::max(surfaceDepth, 5);

            // ---- Fast path de columnas --------------------------------
            // (ver el bloque de comentarios arriba de Normal::Normal).
            // El mismo bucle, con la seccion resuelta por-bloque -> por-
            // seccion: la seccion (uint8_t)(y >> 4) es la misma para todo
            // un sub-rango de Y, asi que solo cambia al cruzar y=16n.
            //
            // h esta clamped a [8,124] por heightFromRaw, asi que el
            // relleno vive entero en las secciones 0..7 y el loop
            // externo puede iterar sy de 0 a (h-1)>>4 sin tocar nada
            // mas alto. El orden de escritura es EXACTAMENTE el del
            // loop original (y = 0..h-1), y los mismos invariantes de
            // height_map / non_air_top / dirty se aplican al final.
            {
                const int col = x + (z << 4);
                mcp_chunk_section_t* s = nullptr;
                int syIdx = -1;
                const uint8_t topId = (int)BlockId::BEDROCK;
                for (int y = 0; y < h; ++y) {
                    uint8_t id;
                    if (y == 0)                       id = (uint8_t)BlockId::BEDROCK;
                    else if (y < h - surfaceDepth)    id = (uint8_t)BlockId::STONE;
                    else if (y < h - 1)               id = filler;
                    else                              id = top;
                    // Stone runs right through the terrain layers, so a
                    // cliff or a cut bank shows rock instead of an
                    // unbroken dirt shell. Bands of it appear at random
                    // depths the way vanilla mixes stone into the
                    // subsurface.
                    if (id == (uint8_t)BlockId::STONE && y < h - 1 && y > 4) {
                        if (((unsigned)(wx * 73856093 ^ wz * 19349663 ^ y * 83492791)) % 23u == 0u)
                            id = (uint8_t)BlockId::DIRT;
                    }
                    const int sy = (unsigned)y >> 4;
                    if (sy != syIdx) {
                        s = section8(c, sy);
                        syIdx = sy;
                    }
                    s->blocks[(x & 15) | ((z & 15) << 4) | ((y & 15) << 8)] = id;
                }
                (void)topId;
            }

            // Water up to sea level (oceans and rivers alike — same level).
            for (int y = h; y < kSeaLevel; ++y)
                mcp_chunk_set_block_id(c, x, y, z, (uint8_t)BlockId::WATER);

            // Cold biome water surface freezes.
            if (snowy && h < kSeaLevel)
                mcp_chunk_set_block_id(c, x, kSeaLevel - 1, z, (uint8_t)BlockId::ICE);

            // Snow layer on top of cold grass above water level. The ground
            // block sits at h-1, so the snow layer goes at h.
            if (snowy && h >= kSeaLevel && h < 100 &&
                mcp_chunk_get_block_id(c, x, h - 1, z) == (uint8_t)BlockId::GRASS) {
                mcp_chunk_set_block_id(c, x, h, z, (uint8_t)BlockId::SNOW_LAYER);
            }

            mcp_chunk_set_biome(c, x, z, b);
            mcp_chunk_set_height_map(c, x, z, h);
        }
    }

    c->terrain_populated = true;
    c->dirty = true;
    // Light/heightmap final pass runs in the caller after generate()+populate().
}

void Normal::populate(GenerationContext& ctx) {
    for (auto& p : populators_) p->populate(ctx);
}

} // namespace cursestone::world::generator
