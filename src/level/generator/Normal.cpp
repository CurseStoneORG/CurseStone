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
#include "block/BlockIds.h"
#include <cmath>

namespace cursestone::level::generator {

using namespace biome;
using object::OreVein;

namespace {

constexpr int kSeaLevel = 64;

// Classify a land column into a biome from its climate values. h0 is the
// raw terrain height (before ocean/beach classification).
uint8_t landBiome(double t, double hu, int h0) {
    if (h0 >= 86) return BIOME_EXTREME_HILLS;
    if (t < -0.45) return BIOME_ICE_PLAINS;
    if (t < -0.20) return (hu < 0.0) ? BIOME_TAIGA : BIOME_FOREST;
    if (hu > 0.5 && h0 < 70) return BIOME_SWAMPLAND;
    if (t > 0.55) return (hu > 0.1) ? BIOME_JUNGLE : BIOME_DESERT;
    if (t > 0.30) return (hu > 0.2) ? BIOME_JUNGLE : BIOME_SAVANNA;
    if (hu > 0.25) return BIOME_FOREST;
    if (hu < -0.15) return BIOME_PLAINS;
    return BIOME_FOREST;
}

bool isColdBiome(uint8_t b) {
    switch (b) {
        case BIOME_TAIGA:
        case BIOME_TAIGA_HILLS:
        case BIOME_ICE_PLAINS:
        case BIOME_ICE_MOUNTAINS:
            return true;
        default:
            return false;
    }
}

uint8_t topForBiome(uint8_t b, int h) {
    using cursestone::block::BlockId;
    switch (b) {
        case BIOME_DESERT:
        case BIOME_DESERT_HILLS:
            return (uint8_t)BlockId::SAND;
        case BIOME_BEACH:
            return (uint8_t)BlockId::SAND;
        case BIOME_OCEAN:
            return (h > 58) ? (uint8_t)BlockId::SAND : (uint8_t)BlockId::GRAVEL;
        case BIOME_EXTREME_HILLS:
            return (h > 82) ? (uint8_t)BlockId::STONE : (uint8_t)BlockId::GRASS;
        default:
            if (isColdBiome(b) && h > 68) return (uint8_t)BlockId::SNOW_BLOCK;
            return (uint8_t)BlockId::GRASS;
    }
}

uint8_t fillerForBiome(uint8_t b) {
    using cursestone::block::BlockId;
    switch (b) {
        case BIOME_DESERT:
        case BIOME_DESERT_HILLS:
            return (uint8_t)BlockId::SANDSTONE;
        case BIOME_BEACH:
            return (uint8_t)BlockId::SAND;
        case BIOME_OCEAN:
            return (uint8_t)BlockId::GRAVEL;
        case BIOME_EXTREME_HILLS:
            return (uint8_t)BlockId::STONE;
        default:
            return (uint8_t)BlockId::DIRT;
    }
}

} // namespace

Normal::Normal(int64_t seed) : seed_(seed) {
    height_noise_ = mcp_simplex_new(seed);
    temp_noise_   = mcp_simplex_new(seed ^ 0xBA5EBA11);
    humid_noise_  = mcp_simplex_new(seed ^ 0x0FF1CE);

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
    // then lakes, then vegetation on the final surface.
    populators_.push_back(std::make_unique<populator::PopulatorOre>(std::move(ores)));
    populators_.push_back(std::make_unique<populator::PopulatorCave>(16));
    populators_.push_back(std::make_unique<populator::PopulatorLake>(2, 9));   // water
    populators_.push_back(std::make_unique<populator::PopulatorLake>(1, 11));  // lava

    populators_.push_back(std::make_unique<populator::PopulatorTree>(10));
    populators_.push_back(std::make_unique<populator::PopulatorTallGrass>(10));
    populators_.push_back(std::make_unique<populator::PopulatorDoublePlant>(2, 2)); // large grass
    populators_.push_back(std::make_unique<populator::PopulatorFlower>(37, 4)); // dandelion
    populators_.push_back(std::make_unique<populator::PopulatorFlower>(38, 3)); // poppy
    populators_.push_back(std::make_unique<populator::PopulatorCactus>(3));
    populators_.push_back(std::make_unique<populator::PopulatorDeadBush>(2));
    populators_.push_back(std::make_unique<populator::PopulatorSugarCane>(6));
    populators_.push_back(std::make_unique<populator::PopulatorMushroom>(1, 39)); // brown
    populators_.push_back(std::make_unique<populator::PopulatorMushroom>(1, 40)); // red
    populators_.push_back(std::make_unique<populator::PopulatorPumpkin>(1));
    populators_.push_back(std::make_unique<populator::PopulatorMelon>(1));
    populators_.push_back(std::make_unique<populator::PopulatorWaterLily>(4));
}

Normal::~Normal() {
    mcp_simplex_destroy(height_noise_);
    mcp_simplex_destroy(temp_noise_);
    mcp_simplex_destroy(humid_noise_);
}

void Normal::generate(GenerationContext& ctx) {
    using cursestone::block::BlockId;
    mcp_chunk_t* c = ctx.chunk;
    const int baseX = ctx.chunk_x * 16;
    const int baseZ = ctx.chunk_z * 16;

    int heights[16][16];
    uint8_t biomes[16][16];

    // Pass 1: raw height + climate biome.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            // Detail noise (hills) + a much lower-frequency continent noise
            // (oceans vs. land). Combining both gives real oceans, beaches
            // and mountains instead of a nearly uniform plain.
            double wx = (baseX + x) * 0.008;
            double wz = (baseZ + z) * 0.008;
            double n = mcp_simplex_fbm2d(height_noise_, wx, wz, 3, 0.35, 2.0);
            double cx = (baseX + x) * 0.0016;
            double cz = (baseZ + z) * 0.0016;
            double cnt = mcp_simplex_fbm2d(height_noise_, cx, cz, 2, 0.5, 2.0);
            int h = 64 + (int)((n + 1.0) * 10.0) + (int)(cnt * 26.0); // ~42..98

            double t  = mcp_simplex_fbm2d(temp_noise_,  (baseX + x) * 0.004, (baseZ + z) * 0.004, 2, 0.5, 2.0);
            double hu = mcp_simplex_fbm2d(humid_noise_, (baseX + x) * 0.005, (baseZ + z) * 0.005, 2, 0.5, 2.0);

            uint8_t b = landBiome(t, hu, h);
            if (b == BIOME_EXTREME_HILLS) {
                h += (h - 86);                               // push peaks up
                if (h > 118) h = 118;
            } else if (b == BIOME_OCEAN || h < kSeaLevel - 2) {
                b = BIOME_OCEAN;
            }

            heights[x][z] = h;
            biomes[x][z] = b;
        }
    }

    // Pass 2: ocean + beach classification. A land column at/just above sea
    // level next to an ocean becomes a beach.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            if (heights[x][z] < kSeaLevel - 2) { biomes[x][z] = BIOME_OCEAN; continue; }
            if (biomes[x][z] == BIOME_OCEAN) continue;
            if (heights[x][z] > kSeaLevel + 2) continue;
            const int dx[4] = { -1, 1, 0, 0 };
            const int dz[4] = { 0, 0, -1, 1 };
            for (int i = 0; i < 4; ++i) {
                int nx = x + dx[i], nz = z + dz[i];
                if (nx < 0 || nx > 15 || nz < 0 || nz > 15) continue;
                if (heights[nx][nz] < kSeaLevel - 2) { biomes[x][z] = BIOME_BEACH; break; }
            }
        }
    }

    // Pass 3: fill columns.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int h = heights[x][z];
            uint8_t b = biomes[x][z];
            uint8_t top    = topForBiome(b, h);
            uint8_t filler = fillerForBiome(b);
            bool cold = isColdBiome(b);
            bool desert = (b == BIOME_DESERT || b == BIOME_DESERT_HILLS);

            for (int y = 0; y < h; ++y) {
                uint8_t id;
                if (y == 0)          id = (uint8_t)BlockId::BEDROCK;
                else if (y < h - 4)  id = (uint8_t)BlockId::STONE;
                else if (y < h - 1)  id = filler;
                else                 id = top;
                mcp_chunk_set_block_id(c, x, y, z, id);
            }

            // Water up to sea level.
            for (int y = h; y < kSeaLevel; ++y)
                mcp_chunk_set_block_id(c, x, y, z, (uint8_t)BlockId::WATER);

            // Cold biome water surface freezes.
            if (cold && h < kSeaLevel) {
                mcp_chunk_set_block_id(c, x, kSeaLevel - 1, z, (uint8_t)BlockId::ICE);
            }

            // Snow layer on top of cold, above-water grass.
            if (cold && h >= kSeaLevel && h < 100 &&
                mcp_chunk_get_block_id(c, x, h, z) == (uint8_t)BlockId::GRASS) {
                mcp_chunk_set_block_id(c, x, h + 1, z, (uint8_t)BlockId::SNOW_LAYER);
            }

            (void)desert;
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
