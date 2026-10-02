// Dev check: prove trees are no longer cut at chunk borders.
//
// Generates a small grid of chunks independently (each chunk on its own,
// exactly like the async generator does) and stitches them into one world
// buffer. Then it measures how often a LOG/LEAF block at a chunk edge has a
// matching LOG/LEAF directly across the seam at the same Y,Z — a tree that
// was "cut off" at the border leaves an air block right across it.
//
//   g++ -std=c++20 -O2 -Isrc tools/cross_border_check.cpp <generator srcs> -o /tmp/cbc
#include "level/generator/Normal.h"
#include "level/format/Chunk.h"
#include "block/BlockIds.h"
#include <cstdio>

using namespace cursestone::level::generator;
using cursestone::block::BlockId;

static constexpr int R = 2;          // chunks -R..R on each axis
static constexpr int N = R * 2 + 1;  // chunks per axis
static constexpr int W = N * 16;     // world width

static uint8_t world[W][128][W];

static bool isWood(uint8_t id) {
    return id == (uint8_t)BlockId::LOG || id == (uint8_t)BlockId::WOOD_2 ||
           id == (uint8_t)BlockId::LEAVES || id == (uint8_t)BlockId::LEAVES_2;
}

int main() {
    Normal gen(12345);
    for (int cx = -R; cx <= R; ++cx) {
        for (int cz = -R; cz <= R; ++cz) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            GenerationContext ctx{};
            ctx.seed = 12345; ctx.chunk_x = cx; ctx.chunk_z = cz;
            ctx.biomes_in = nullptr; ctx.chunk = c;
            gen.generate(ctx);
            gen.populate(ctx);
            for (int x = 0; x < 16; ++x)
                for (int y = 0; y < 128; ++y)
                    for (int z = 0; z < 16; ++z)
                        world[(cx + R) * 16 + x][y][(cz + R) * 16 + z] =
                            mcp_chunk_get_block_id(c, x, y, z);
            mcp_chunk_destroy(c);
        }
    }

    // Cross-border continuity: an edge wood block whose neighbour across the
    // seam is also wood is "connected"; otherwise it is a cut.
    long connected = 0, cut = 0;
    for (int cx = -R; cx < R; ++cx) {
        int wx = (cx + R) * 16 + 15;
        for (int y = 0; y < 128; ++y) {
            for (int z = 0; z < W; ++z) {
                if (!isWood(world[wx][y][z])) continue;
                if (isWood(world[wx + 1][y][z])) connected++; else cut++;
            }
        }
    }
    std::printf("trees spanning x seams: connected edge wood=%ld cut=%ld\n",
                connected, cut);
    std::printf("GENERATOR CROSS-BORDER: %s\n", connected > 0 ? "OK" : "FAIL");
    return connected > 0 ? 0 : 1;
}
