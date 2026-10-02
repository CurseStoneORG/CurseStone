#include "Normal.h"
#include "level/format/Chunk.h"
#include "level/generator/populator/PopulatorOre.h"
#include "level/generator/populator/PopulatorTree.h"
#include "level/generator/populator/PopulatorTallGrass.h"
#include "level/generator/populator/PopulatorFlower.h"
#include "level/generator/populator/PopulatorCave.h"
#include "level/generator/populator/PopulatorFeatures.h"
#include "level/generator/object/OreGenerator.h"
#include "level/generator/biome/Biome.h"
#include "level/generator/biome/BiomeRegistry.h"
#include "block/BlockIds.h"
#include <algorithm>
#include <cmath>

namespace cursestone::level::generator {

using namespace biome;
using object::OreVein;

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
}

Normal::~Normal() = default;

void Normal::generate(GenerationContext& ctx) {
    using cursestone::block::BlockId;
    mcp_chunk_t* c = ctx.chunk;
    const int baseX = ctx.chunk_x * 16;
    const int baseZ = ctx.chunk_z * 16;

    // ---- Height field ----------------------------------------------------
    // Raw column heights are a pure function of world coordinates, computed
    // on an 18x18 grid (1-block margin) so the smoothing kernel and the
    // beach/water neighbour test are identical at every chunk border. Two
    // neighbouring chunks therefore agree on every shared column.
    constexpr int GW = 18;   // world offsets -1 .. 16
    double raw[GW][GW];
    for (int i = 0; i < GW; ++i)
        for (int j = 0; j < GW; ++j)
            raw[i][j] = rawColumnHeight(noise_, baseX - 1 + i, baseZ - 1 + j);

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
            biomes[x][z] = columnBiome(noise_, baseX + x, baseZ + z, heights[x][z]);

    // ---- Fill columns ----------------------------------------------------
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int h = heights[x][z];
            uint8_t b = biomes[x][z];
            const BiomeConfig& cfg = BiomeRegistry::forId(b);

            uint8_t top    = cfg.topBlock;
            uint8_t filler = cfg.fillerBlock;

            // Vanilla-ish surface extras per biome.
            if (b == BIOME_OCEAN) {
                top = (h > 58) ? (uint8_t)BlockId::SAND : (uint8_t)BlockId::GRAVEL;
                filler = (uint8_t)BlockId::GRAVEL;
            } else if (b == BIOME_RIVER || b == BIOME_FROZEN_RIVER) {
                top = (uint8_t)BlockId::SAND;
                filler = (h > 58) ? (uint8_t)BlockId::SAND : (uint8_t)BlockId::GRAVEL;
            } else if (b == BIOME_EXTREME_HILLS) {
                // Rocky tops above the tree line, grass/dirt below.
                if (h >= 86) { top = (uint8_t)BlockId::STONE; filler = (uint8_t)BlockId::STONE; }
            }

            const bool snowy = cfg.snowy || b == BIOME_FROZEN_RIVER;

            for (int y = 0; y < h; ++y) {
                uint8_t id;
                if (y == 0)          id = (uint8_t)BlockId::BEDROCK;
                else if (y < h - 4)  id = (uint8_t)BlockId::STONE;
                else if (y < h - 1)  id = filler;
                else                 id = top;
                mcp_chunk_set_block_id(c, x, y, z, id);
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

} // namespace cursestone::level::generator
