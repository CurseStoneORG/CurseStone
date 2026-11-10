// tools/vgen_village_furnace_check.cpp
// E2E guard for the village smithy furnace removal: the 0.15.10 client
// crashes when it touches a furnace, so structure generation must not
// place any. Locates real village centers for the world seed through the
// live VillageFeature placement math, generates a full chunk window
// around each one through the actual VgenSource pipeline
// (generate + populate + spill drain), then asserts:
//   1) at least one village was found,
//   2) the villages really built (planks + cobblestone present),
//   3) ZERO furnace blocks (id 61/62) exist anywhere in the generated
//      terrain — this is what fails before the removal and passes after.
//
//   g++ -std=c++20 -O2 -Isrc -Isrc/world/generator \
//       tools/vgen_village_furnace_check.cpp <repo objects> -o /tmp/vgen_village_furnace_check
#include "world/generator/vgen/VgenSource.h"
#include "world/generator/Generator.h"
#include "world/generator/vgen/structure/VillageFeature.h"
#include "world/generator/vgen/biome/BiomeSource.h"
#include "world/format/Chunk.h"
#include "block/BlockIds.h"

#include <cstdio>
#include <vector>

using namespace cursestone::world;
using namespace cursestone::world::generator;
using cursestone::block::BlockId;

namespace {

constexpr int kScanRadius = 120;      // chunk radius of the village search
constexpr int kWindowRadius = 8;      // chunks generated around each village

struct Window {
    int cx, cz;
    long furnaces = 0;
    long planks = 0;
    long cobble = 0;
};

// One furnace block anywhere in a generated chunk counts, whichever
// structure or spill wrote it.
void scanChunk(mcp_chunk_t* c, Window& w) {
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 128; ++y) {
                const uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
                if (id == (uint8_t)BlockId::FURNACE || id == (uint8_t)BlockId::BURNING_FURNACE)
                    ++w.furnaces;
                else if (id == (uint8_t)BlockId::PLANKS)
                    ++w.planks;
                else if (id == (uint8_t)BlockId::COBBLESTONE)
                    ++w.cobble;
            }
}

Window generateWindow(VgenSource& gen, int64_t seed, int vcx, int vcz) {
    Window w{vcx, vcz};
    for (int cx = vcx - kWindowRadius; cx <= vcx + kWindowRadius; ++cx) {
        for (int cz = vcz - kWindowRadius; cz <= vcz + kWindowRadius; ++cz) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            GenerationContext ctx{seed, cx, cz, nullptr, c};
            gen.generate(ctx);
            gen.populate(ctx);
            gen.drainSpills(cx, cz, c);
            c->terrain_populated = true;
            mcp_chunk_recalculate_height_map(c);
            mcp_chunk_propagate_sky_light(c);
            scanChunk(c, w);
            mcp_chunk_destroy(c);
        }
    }
    return w;
}

} // namespace

int main(int argc, char** argv) {
    std::fprintf(stderr, "[check] main entered\n");
    const int64_t seeds[] = {1337, 777, 55, 12345, 4242, 2026, 99, 31337};
    const int64_t seed = argc > 1 ? atoll(argv[1]) : 0;

    // The biome/block registries are lazily initialised by the generator
    // ctor; the village scan reads them, so pay that cost up front.
    std::fprintf(stderr, "[check] warmup begin\n");
    { VgenSource warmup(argc > 1 ? seed : seeds[0]); }
    std::fprintf(stderr, "[check] warmup done\n");

    std::vector<std::pair<int, int>> villages;
    int64_t worldSeed = seed;
    if (seed != 0) {
        // Single explicit seed: find its villages or fail.
        vgen::BiomeSource biomes((vgen::RandomSeed)seed, vgen::GeneratorType::Overworld);
        vgen::VillageFeature feature;
        vgen::Random rng;
        for (int x = -kScanRadius; x <= kScanRadius; ++x)
            for (int z = -kScanRadius; z <= kScanRadius; ++z)
                if (feature.isFeatureChunk(&biomes, rng, vgen::ChunkPos(x, z)))
                    villages.emplace_back(x, z);
    } else {
        // Pick the first stock seed that actually has a village in range.
        for (int64_t s : seeds) {
            vgen::BiomeSource biomes((vgen::RandomSeed)s, vgen::GeneratorType::Overworld);
            vgen::VillageFeature feature;
            vgen::Random rng;
            villages.clear();
            for (int x = -kScanRadius; x <= kScanRadius; ++x)
                for (int z = -kScanRadius; z <= kScanRadius; ++z)
                    if (feature.isFeatureChunk(&biomes, rng, vgen::ChunkPos(x, z)))
                        villages.emplace_back(x, z);
            if (!villages.empty()) {
                worldSeed = s;
                break;
            }
        }
    }

    if (villages.empty()) {
        std::printf("FAIL: no village center within %d chunks (seed %lld)\n",
                    kScanRadius, (long long)worldSeed);
        return 1;
    }
    if (villages.size() > 3)
        villages.resize(3); // three windows are plenty of evidence
    std::printf("seed %lld: %zu village center(s) in scan, generating %zu window(s)\n",
                (long long)worldSeed, villages.size() - (argc > 1 ? 0 : 0), villages.size());

    VgenSource gen(worldSeed);

    long totalFurnaces = 0;
    bool villageBuilt = false;
    for (auto [vcx, vcz] : villages) {
        Window w = generateWindow(gen, worldSeed, vcx, vcz);
        std::printf("  village @ chunk (%d,%d): furnaces=%ld planks=%ld cobble=%ld\n",
                    w.cx, w.cz, w.furnaces, w.planks, w.cobble);
        totalFurnaces += w.furnaces;
        if (w.planks > 0 && w.cobble > 0)
            villageBuilt = true;
    }

    bool ok = true;
    if (!villageBuilt) {
        std::printf("FAIL: generated window(s) contain no village materials\n");
        ok = false;
    }
    if (totalFurnaces != 0) {
        std::printf("FAIL: %ld furnace block(s) generated inside village windows\n",
                    totalFurnaces);
        ok = false;
    }
    std::printf(ok ? "VILLAGE FURNACE CHECK: OK\n" : "VILLAGE FURNACE CHECK: FAIL\n");
    return ok ? 0 : 1;
}
