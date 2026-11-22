/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// tools/gen_equiv_check.cpp — vanilla generation equivalence check.
//
// Generates a fixed block of chunks straight through the real vanilla
// generator (VgenSource: terrain + caves + populate, the exact bytes the
// server would stream) and prints one digest line per chunk plus a total.
// Two runs digest-equal => identical block+data bytes.
//
// Used two ways:
//   1. SIMD equivalence: same tree, CURSESTONE_NOISE_SCALAR=1 vs unset.
//   2. Vanilla preservation: current tree vs the baseline commit tree
//      (both trees print identical digests => generation untouched).
//
//   g++ -std=c++20 -O2 -w -Isrc -Isrc/world/generator \
//       -o gen_equiv tools/gen_equiv_check.cpp <sources, see compile.sh>
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"
#include "world/generator/Generator.h"
#include "world/generator/vgen/VgenSource.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

using cursestone::world::generator::GenerationContext;
using cursestone::world::generator::VgenSource;

static uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ULL) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

int main(int argc, char** argv) {
    const int64_t seed = argc > 1 ? atoll(argv[1]) : 12345;
    const int radius = argc > 2 ? atoi(argv[2]) : 6; // chunks [-r, r]

    VgenSource gen(seed);

    uint64_t total = 1469598103934665603ULL;
    int chunks = 0;
    for (int cx = -radius; cx <= radius; ++cx) {
        for (int cz = -radius; cz <= radius; ++cz) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            if (!c) {
                std::printf("FAIL: chunk alloc\n");
                return 1;
            }
            for (int bx = 0; bx < 16; ++bx)
                for (int bz = 0; bz < 16; ++bz)
                    mcp_chunk_set_biome(c, bx, bz, 1);

            GenerationContext ctx;
            ctx.seed = seed;
            ctx.chunk_x = cx;
            ctx.chunk_z = cz;
            ctx.biomes_in = nullptr;
            ctx.chunk = c;
            gen.generate(ctx);
            gen.populate(ctx);
            c->terrain_populated = true;
            c->dirty = true;
            mcp_chunk_recalculate_height_map(c);
            mcp_chunk_propagate_sky_light(c);

            uint64_t h = 1469598103934665603ULL;
            for (int s = 0; s < 8; ++s) {
                const mcp_chunk_section_t* sec = mcp_chunk_get_section(c, (uint8_t)s);
                if (!sec || !sec->blocks) {
                    h = fnv1a("air", 3, h);
                    continue;
                }
                h = fnv1a(sec->blocks, 4096, h);
                h = fnv1a(sec->data ? sec->data : (const void*)"nodata", sec->data ? 2048 : 6, h);
            }
            if (c->biomes)
                h = fnv1a(c->biomes, 256, h);
            std::printf("chunk %d %d %016llx\n", cx, cz,
                        (unsigned long long)h);
            total = fnv1a(&h, sizeof(h), total);
            mcp_chunk_destroy(c);
            ++chunks;
        }
    }
    std::printf("TOTAL chunks=%d seed=%lld radius=%d digest=%016llx\n",
                chunks, (long long)seed, radius, (unsigned long long)total);
    return 0;
}
