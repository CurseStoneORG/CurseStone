// tools/vgen_dump_hash.cpp
// Deterministic generation dump: generates a fixed grid of chunks through
// the REAL generation flow (generate + populate + heightmap + sky fill) and
// prints a stable hash per chunk plus a global hash. Used to A/B generator
// refactors: the printed hashes must be IDENTICAL before and after a
// change that claims to be output-preserving.
//
// Usage: vgen_dump_hash [N chunks per side, default 8] [seed, default 1337]
#include "world/generator/vgen/VgenSource.h"
#include "world/generator/Generator.h"
#include "world/format/Chunk.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>

using namespace cursestone::world;
using namespace cursestone::world::generator;

static uint64_t fnv1a(uint64_t h, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

int main(int argc, char** argv) {
    const int SIDE = argc > 1 ? atoi(argv[1]) : 8; // SIDE x SIDE chunks
    const int64_t seed = argc > 2 ? atoll(argv[2]) : 1337;

    auto gen = std::make_shared<VgenSource>(seed);

    uint64_t global = 1469598103934665603ULL;
    for (int cz = 0; cz < SIDE; ++cz) {
        for (int cx = 0; cx < SIDE; ++cx) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            GenerationContext ctx{seed, cx, cz, nullptr, c};
            gen->generate(ctx);
            gen->populate(ctx);
            c->terrain_populated = true;
            mcp_chunk_recalculate_height_map(c);
            mcp_chunk_propagate_sky_light(c);

            uint64_t h = 1469598103934665603ULL;
            for (int s = 0; s < 16; ++s) {
                const mcp_chunk_section_t* sec = c->sections[s];
                if (!sec || !sec->blocks)
                    continue;
                h = fnv1a(h, sec->blocks, 4096);
                if (sec->data)
                    h = fnv1a(h, sec->data, 2048);
            }
            if (c->biomes)
                h = fnv1a(h, c->biomes, 256);
            if (c->height_map)
                h = fnv1a(h, c->height_map, 256);
            printf("chunk(%2d,%2d) hash=%016llx\n", cx, cz, (unsigned long long)h);
            global = fnv1a(global, &h, sizeof(h));
            mcp_chunk_destroy(c);
        }
    }
    printf("GLOBAL hash=%016llx\n", (unsigned long long)global);
    return 0;
}
