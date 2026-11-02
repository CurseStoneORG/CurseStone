// tools/light_oracle.cpp
//
// Convergence judge for the LightEngine. Builds THREE identical worlds
// (same seed -> byte-identical terrain), installs the whole chunk field
// in each, and brings the light to its fixed point three different
// ways:
//
//   FULL   — the exact path: every chunk queued as a Full entry (the
//            46x46 clear-and-reflood sweep, chunk + 15 into neighbours).
//   BORDER — the cheap path: every chunk queued as a Border entry
//            (pull / interior-flood / push with neighbour re-queues).
//   ORACLE — one relightBoxExact over the entire field: a single global
//            clear + reseed + flood. No queueing, no chunk boundaries
//            involved at all.
//
// At the fixed point all three MUST agree cell for cell, on both
// layers. BORDER falling short of ORACLE means light that crosses two
// chunk frontiers died on the way (the failure mode that motivated the
// push->touched->interiorFlood chain); FULL differing from ORACLE means
// the sweep itself is broken.
//
//   Exit 0: FULL == BORDER == ORACLE, 0 differing cells.
//   Exit 1: any difference (prints the first ones with coordinates).
//   Exit 2: a drain never settled (convergence failure).
//
//   g++ -std=c++20 -O2 -Isrc -o /tmp/light_oracle tools/light_oracle.cpp \
//       $(objects except src_CurseStone.cpp.o) -lz -ldl -pthread
#include "Server.h"
#include "world/World.h"
#include "world/format/Chunk.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace cursestone;
using namespace cursestone::world;
namespace fs = std::filesystem;

static World* makeWorld(const std::string& name, int64_t seed) {
    std::string root = "/tmp/cs_light_oracle";
    fs::create_directories(root + "/worlds/" + name + "/region");
    WorldConfig lc;
    lc.name = name;
    lc.format = "anvil";
    lc.generator = "normal";
    lc.seed = seed;
    lc.dimension = 0;
    lc.dataPath = root;
    return new World(std::move(lc));
}

struct Field {
    int32_t lo, hi; // chunk range [lo, hi]
};

static void installField(World* w, const Field& f) {
    for (int cx = f.lo; cx <= f.hi; ++cx)
        for (int cz = f.lo; cz <= f.hi; ++cz)
            w->chunks().getChunk(cx, cz);
}

static void drainFully(World* w, const char* tag) {
    size_t guard = 0;
    while (w->hasPendingLight()) {
        w->drainLightPending(4096, 0); // no wall-clock cap in the judge
        if (++guard > 20000) {
            printf("[%s] FAIL: light queue never settled (guard hit)\n", tag);
            exit(2);
        }
    }
}

struct Diff {
    size_t count = 0;
    int shown = 0;
    void report(const char* tag, const char* layer, int32_t wx, int32_t y, int32_t wz, int a,
                int b) {
        ++count;
        if (shown < 10) {
            ++shown;
            printf("  [%s] %s diff at (%d, %d, %d): %d vs oracle %d\n", tag, layer, wx, y, wz, a, b);
        }
    }
};

static Diff compare(World* w, World* oracle, const Field& f, const char* tag) {
    Diff d;
    for (int cx = f.lo; cx <= f.hi; ++cx)
        for (int cz = f.lo; cz <= f.hi; ++cz) {
            mcp_chunk_t* a = w->chunks().getChunk(cx, cz);
            mcp_chunk_t* b = oracle->chunks().getChunk(cx, cz);
            if (!a || !b) {
                printf("  [%s] missing chunk (%d, %d)\n", tag, cx, cz);
                ++d.count;
                continue;
            }
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 16; ++x)
                    for (int z = 0; z < 16; ++z) {
                        const int32_t wx = cx * 16 + x, wz = cz * 16 + z;
                        const int sa = mcp_chunk_get_sky_light(a, x, y, z);
                        const int sb = mcp_chunk_get_sky_light(b, x, y, z);
                        if (sa != sb)
                            d.report(tag, "sky", wx, y, wz, sa, sb);
                        const int ba = mcp_chunk_get_block_light(a, x, y, z);
                        const int bb = mcp_chunk_get_block_light(b, x, y, z);
                        if (ba != bb)
                            d.report(tag, "blk", wx, y, wz, ba, bb);
                    }
        }
    return d;
}

int main(int argc, char** argv) {
    const int side = argc > 1 ? atoi(argv[1]) : 17;
    const int64_t seed = 1337;
    Field f{-side / 2, side - side / 2 - 1};

    printf("oracle: %dx%d chunk field, seed %lld\n", side, side, (long long)seed);

    // --- ORACLE world: one exact global re-flood ------------------------
    World* wo = makeWorld("oracle", seed);
    installField(wo, f);
    {
        const int32_t x0 = f.lo * 16, z0 = f.lo * 16;
        const int32_t x1 = f.hi * 16 + 15, z1 = f.hi * 16 + 15;
        wo->light().relightBoxExact(x0, 0, z0, x1, 127, z1);
    }

    // --- FULL world: every chunk takes the exact sweep ------------------
    World* wf = makeWorld("full", seed);
    installField(wf, f);
    for (int cx = f.lo; cx <= f.hi; ++cx)
        for (int cz = f.lo; cz <= f.hi; ++cz)
            wf->onChunkAdded(cx, cz, /*full=*/true);
    drainFully(wf, "FULL");

    // --- BORDER world: every chunk takes the cheap border entry ---------
    World* wb = makeWorld("border", seed);
    installField(wb, f);
    for (int cx = f.lo; cx <= f.hi; ++cx)
        for (int cz = f.lo; cz <= f.hi; ++cz)
            wb->onChunkAdded(cx, cz, /*full=*/false);
    drainFully(wb, "BORDER");

    Diff df = compare(wf, wo, f, "FULL");
    Diff db = compare(wb, wo, f, "BORDER");

    const bool ok = df.count == 0 && db.count == 0;
    printf("FULL vs ORACLE: %zu differing cells\n", df.count);
    printf("BORDER vs ORACLE: %zu differing cells\n", db.count);
    printf(ok ? "ORACLE: ALL THREE PATHS AGREE\n" : "ORACLE: MISMATCH\n");

    delete wb;
    delete wf;
    delete wo;
    return ok ? 0 : 1;
}
