// tools/portal_check.cpp
//
// Regression checks for the Nether generator and the portal travel
// support, without a network client:
//   * Nether terrain: bedrock floor + roof (jagged), lava sea, cavern
//     belt, HELL biome, features (quartz, soul sand, gravel, glowstone)
//   * nether world spawn lands inside the caverns, not on the roof
//   * PortalRegistry: dedupe, radius search, weighted Y, remove,
//     save/load round trip, World load-on-boot integration
//
//   Link against the real build objects (same pattern as the other
//   tools/ checks), from the build directory:
//   g++ -std=c++20 -Isrc -o /tmp/portal_check tools/portal_check.cpp \
//       $(ls build/CMakeFiles/CurseStone.dir/src/*.o \
//          build/CMakeFiles/CurseStone.dir/src/**/*.o \
//          | grep -v "src/CurseStone.cpp.o") -lz -ldl -pthread
//   /tmp/portal_check
#include "Server.h"
#include "world/World.h"
#include "world/PortalRegistry.h"
#include "block/BlockIds.h"
#include <cstdio>
#include <filesystem>
#include <string>

using namespace cursestone;
using namespace cursestone::world;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok  - %s\n", msg); } \
    else { printf("  FAIL- %s\n", msg); ++failures; } } while (0)

// getBlockId/setBlock do not generate chunks: load them by hand.
static void loadArea(World* w, int x0, int z0, int x1, int z1) {
    for (int cx = x0 >> 4; cx <= x1 >> 4; ++cx)
        for (int cz = z0 >> 4; cz <= z1 >> 4; ++cz)
            w->chunks().getChunk(cx, cz);
}

static World* makeWorld(const std::string& name, const std::string& gen,
                        int dim, int64_t seed) {
    std::string root = "/tmp/cs_portal_check";
    fs::create_directories(root + "/worlds/" + name + "/region");
    WorldConfig lc;
    lc.name = name;
    lc.format = "anvil";
    lc.generator = gen;
    lc.seed = seed;
    lc.dimension = dim;
    lc.dataPath = root;
    return new World(std::move(lc));
}

int main() {
    // ---------- nether terrain (5x5 chunks around the spawn) ----------
    World* wn = makeWorld("nether", "nether", 1, 777);
    loadArea(wn, -32, -32, 32, 32);

    // Bedrock floor: solid at y=0 across the whole area.
    int bad_floor = 0;
    for (int x = -32; x <= 32; ++x)
        for (int z = -32; z <= 32; ++z)
            if (wn->getBlockId(x, 0, z) != block::BEDROCK) ++bad_floor;
    CHECK(bad_floor == 0, "nether floor is solid bedrock at y=0");

    // Bedrock roof: solid at y=127 across the whole area.
    int bad_roof = 0;
    for (int x = -32; x <= 32; ++x)
        for (int z = -32; z <= 32; ++z)
            if (wn->getBlockId(x, 127, z) != block::BEDROCK) ++bad_roof;
    CHECK(bad_roof == 0, "nether roof is solid bedrock at y=127");

    // Jagged distribution: bedrock also appears inside y=1..4 and
    // y=123..126 (a scan over the area must find both).
    int jag_low = 0, jag_high = 0;
    for (int x = -32; x <= 32; x += 2)
        for (int z = -32; z <= 32; z += 2) {
            for (int y = 1; y <= 4; ++y)
                if (wn->getBlockId(x, y, z) == block::BEDROCK) { ++jag_low; break; }
            for (int y = 123; y <= 126; ++y)
                if (wn->getBlockId(x, y, z) == block::BEDROCK) { ++jag_high; break; }
        }
    CHECK(jag_low > 100, "floor bedrock is jagged (found in y=1..4)");
    CHECK(jag_high > 100, "roof bedrock is jagged (found in y=123..126)");

    // Lava sea: open cells below y=32 hold lava.
    int lava = 0;
    for (int dx = -28; dx <= 28; dx += 2)
        for (int dz = -28; dz <= 28; dz += 2)
            for (int y = 2; y <= 31; ++y)
                if (wn->getBlockId(dx, y, dz) == block::LAVA) { ++lava; break; }
    CHECK(lava > 200, "lava sea present below y=32");

    // Cavern belt: the middle band is not a solid shell.
    int mid_air = 0, mid_total = 0;
    for (int dx = -28; dx <= 28; dx += 4)
        for (int dz = -28; dz <= 28; dz += 4) {
            ++mid_total;
            for (int y = 40; y <= 90; ++y)
                if (wn->getBlockId(dx, y, dz) == block::AIR) { ++mid_air; break; }
        }
    CHECK(mid_air > mid_total / 2, "open cavern belt around the middle");

    // HELL biome on every sampled column.
    int bad_biome = 0;
    for (int dx = -32; dx <= 32; dx += 4)
        for (int dz = -32; dz <= 32; dz += 4) {
            mcp_chunk_t* c = wn->chunks().getChunkIfLoaded(dx >> 4, dz >> 4);
            if (c && mcp_chunk_get_biome(c, dx & 15, dz & 15) != 8) ++bad_biome;
        }
    CHECK(bad_biome == 0, "every nether column uses the HELL biome");

    // Features across the area (each must exist somewhere).
    int quartz = 0, soulsand = 0, gravel = 0, glowstone = 0;
    for (int dx = -32; dx <= 32; ++dx)
        for (int dz = -32; dz <= 32; ++dz)
            for (int y = 1; y <= 126; ++y) {
                uint8_t id = wn->getBlockId(dx, y, dz);
                if (id == block::NETHER_QUARTZ_ORE) ++quartz;
                else if (id == block::SOUL_SAND) ++soulsand;
                else if (id == block::GRAVEL) ++gravel;
                else if (id == block::GLOWSTONE) ++glowstone;
            }
    CHECK(quartz > 0, "nether quartz ore generated");
    CHECK(soulsand > 0, "soul sand patches generated");
    CHECK(gravel > 0, "gravel patches generated");
    CHECK(glowstone > 0, "glowstone clusters generated");

    // Spawn: the nether spawn must sit inside the caverns, not on the
    // bedrock roof.
    double sy = wn->spawn().y;
    CHECK(sy > 0 && sy < 127, "nether spawn is not on the bedrock roof");
    CHECK(wn->getBlockId((int)wn->spawn().x, (int)sy - 1, (int)wn->spawn().z)
              != block::BEDROCK, "nether spawn does not stand on bedrock");

    // ---------- PortalRegistry ----------
    {
        PortalRegistry reg;
        reg.add(100, 40, 200, 0);
        reg.add(101, 40, 201, 0);          // inside the dedupe distance
        CHECK(reg.size() == 1, "duplicate portal entries are collapsed");
        reg.add(300, 40, 200, 1);
        CHECK(reg.size() == 2, "distant portal is kept");

        const auto* hit = reg.findNearest(100.5, 40.5, 200.5, 128.0);
        CHECK(hit && hit->x == 100 && hit->z == 200, "nearest portal found");
        CHECK(!reg.findNearest(1000, 40, 200, 128.0),
              "search radius respected");

        // Vertical offset weighs less than a horizontal one.
        PortalRegistry reg2;
        reg2.add(0, 30, 0, 0);
        reg2.add(0, 60, 0, 0);
        const auto* by = reg2.findNearest(0.5, 59.0, 0.5, 128.0);
        CHECK(by && by->y == 60, "vertical offset is weighted lower");

        CHECK(reg.remove(100, 40, 200), "stale entry removed");
        CHECK(!reg.contains(100, 40, 200), "removed entry is gone");
        CHECK(!reg.remove(100, 40, 200), "removing a missing entry fails");

        CHECK(reg.save("/tmp/cs_portal_check/portals.dat"),
              "registry saves");
        PortalRegistry reg3;
        CHECK(reg3.load("/tmp/cs_portal_check/portals.dat"),
              "registry loads");
        CHECK(reg3.size() == 1 && reg3.contains(300, 40, 200),
              "save/load round trip preserves entries");
    }

    // ---------- World integration: registry loads on boot ----------
    wn->portals().add(5, 33, 5, 0);
    CHECK(wn->portals().contains(5, 33, 5), "portal add into world registry");
    wn->savePortals();
    delete wn;
    World* wn2 = makeWorld("nether", "nether", 1, 777);
    CHECK(wn2->portals().contains(5, 33, 5),
          "world registry persists across restarts");
    delete wn2;

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL OK", failures);
    return failures ? 1 : 0;
}
