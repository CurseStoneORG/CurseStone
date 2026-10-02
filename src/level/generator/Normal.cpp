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
#include <cmath>

namespace cursestone::level::generator {

using namespace biome;
using object::OreVein;

namespace {

constexpr int kSeaLevel = 64;

inline double smoothstep(double e0, double e1, double x) {
    double t = (x - e0) / (e1 - e0);
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
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
    // then lakes, then vegetation on the final surface. Feature populators
    // all use 16 candidate slots per chunk; the actual density comes from
    // each biome's BiomeConfig amounts, so their ctor argument only sets
    // the slot count, not the frequency.
    populators_.push_back(std::make_unique<populator::PopulatorOre>(std::move(ores)));
    populators_.push_back(std::make_unique<populator::PopulatorCave>(16));
    populators_.push_back(std::make_unique<populator::PopulatorLake>(2, 9));   // water
    populators_.push_back(std::make_unique<populator::PopulatorLake>(1, 11));  // lava

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

    // ---- Height field ----------------------------------------------------
    // Three very-low-frequency layers, sampled on a window with a 2-block
    // margin (world columns -2..17) so two separable blur passes stay
    // seamless across chunk borders: the raw noise is a pure function of
    // world coordinates, so neighbouring chunks compute identical values
    // for shared columns.
    //
    //   cont — continent shape (oceans vs. land), wavelength ~900 blocks
    //   hill — relief, wavelength ~240 blocks
    //   det  — gentle surface detail, wavelength ~95 blocks
    //
    // Amplitudes are chosen so the typical per-block gradient stays well
    // under 1: after the blur passes almost every slope is walkable
    // (steps of at most 1 block), instead of the old 2-3 block terraces.
    constexpr int kWin = 20;                 // world offsets -2 .. 17
    double raw[kWin][kWin];

    for (int i = 0; i < kWin; ++i) {
        for (int j = 0; j < kWin; ++j) {
            const double wx = baseX + i - 2;
            const double wz = baseZ + j - 2;

            const double cont = mcp_simplex_fbm2d(height_noise_,
                wx * 0.0011, wz * 0.0011, 3, 0.5, 2.0);
            const double hill = mcp_simplex_fbm2d(height_noise_,
                wx * 0.0042 + 77.3, wz * 0.0042 - 41.7, 3, 0.5, 2.0);
            const double det = mcp_simplex_fbm2d(height_noise_,
                wx * 0.0105 + 900.0, wz * 0.0105 + 300.0, 2, 0.5, 2.0);

            // land: 0 over deep ocean, 1 over the continent.
            const double land = smoothstep(-0.35, -0.02, cont);

            double h = 68.0 + cont * 30.0;              // base curve 38..98
            h += hill * (15.0 * land + 3.0 * (1.0 - land));
            h += det * (2.5 * land + 0.5);
            raw[i][j] = h;
        }
    }

    // Two separable (1,2,1)/4 blur passes over the window. Valid interior
    // after both passes: i in [2, kWin-3] => world x 0..15.
    double blur[kWin][kWin];
    for (int i = 1; i < kWin - 1; ++i)
        for (int j = 0; j < kWin; ++j)
            blur[i][j] = (raw[i - 1][j] + 2.0 * raw[i][j] + raw[i + 1][j]) * 0.25;

    double final[kWin][kWin];
    for (int i = 2; i < kWin - 2; ++i)
        for (int j = 1; j < kWin - 1; ++j)
            final[i][j] = (blur[i][j - 1] + 2.0 * blur[i][j] + blur[i][j + 1]) * 0.25;

    int heights[16][16];
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            heights[x][z] = (int)std::lround(final[x + 2][z + 2]);

    // ---- Climate + biome selection --------------------------------------
    // Temperature/humidity use very low frequencies (features every 400+
    // blocks) and BiomeRegistry::select applies wide climate bands, so a
    // biome spans hundreds of blocks instead of flickering every few
    // metres. The pass below also rewrites the ids so every stored id is a
    // base biome id.
    uint8_t biomes[16][16];
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            const double wx = baseX + x;
            const double wz = baseZ + z;
            const double t  = mcp_simplex_fbm2d(temp_noise_,
                wx * 0.0004, wz * 0.0004, 2, 0.5, 2.0);
            const double hu = mcp_simplex_fbm2d(humid_noise_,
                wx * 0.0005 + 512.0, wz * 0.0005 - 256.0, 2, 0.5, 2.0);
            biomes[x][z] = BiomeRegistry::select(t, hu, heights[x][z], kSeaLevel);
        }
    }

    // Ocean + beach classification: a land column at/just above sea level
    // next to open water becomes a beach.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            if (heights[x][z] < kSeaLevel - 2) { biomes[x][z] = BIOME_OCEAN; continue; }
            if (biomes[x][z] == BIOME_OCEAN) { biomes[x][z] = BIOME_PLAINS; }
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
            } else if (b == BIOME_EXTREME_HILLS) {
                // Rocky tops above the tree line, grass/dirt below.
                if (h >= 86) { top = (uint8_t)BlockId::STONE; filler = (uint8_t)BlockId::STONE; }
            }

            const bool snowy = cfg.snowy;

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
            if (snowy && h < kSeaLevel)
                mcp_chunk_set_block_id(c, x, kSeaLevel - 1, z, (uint8_t)BlockId::ICE);

            // Snow layer on top of cold grass above water level.
            if (snowy && h >= kSeaLevel && h < 100 &&
                mcp_chunk_get_block_id(c, x, h, z) == (uint8_t)BlockId::GRASS) {
                mcp_chunk_set_block_id(c, x, h + 1, z, (uint8_t)BlockId::SNOW_LAYER);
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
