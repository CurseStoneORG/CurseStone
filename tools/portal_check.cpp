// tools/portal_check.cpp
//
// Smoke test del sistema de portales SIN cliente:
//   * tryLightNetherPortal() sobre un marco de obsidiana real (mundo flat)
//   * relleno con PORTAL y meta por eje (1 = X, 2 = Z)
//   * marcos rotos -> no se enciende nada
//   * extinguishPortalAt() apaga la cadena al romper obsidiana
//
//   g++ -std=c++20 -Isrc -o /tmp/portal_check tools/portal_check.cpp \
//       $(ls build/CMakeFiles/CurseStone.dir/src/*.o \
//          build/CMakeFiles/CurseStone.dir/src/**/*.o \
//          | grep -v "src/CurseStone.cpp.o") -lz -ldl -pthread
//   /tmp/portal_check
#include "Server.h"
#include "world/World.h"
#include "world/Portals.h"
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

// getBlockId/setBlock no generan chunks: hay que cargarlos a mano.
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
    // ---------- marco valido en eje X (interior 2x3) ----------
    World* w = makeWorld("flat", "flat", 0, 12345);
    loadArea(w, 0, 0, 40, 16);
    for (int x = 0; x <= 3; ++x)
        for (int y = 9; y <= 13; ++y)
            w->setBlock(x, y, 10, (x == 0 || x == 3 || y == 9 || y == 13)
                                      ? block::OBSIDIAN : block::AIR, 0);
    CHECK(portals::tryLightNetherPortal(w, 1, 10, 10),
          "portal lights inside a valid 2x3 X-frame");
    CHECK(w->getBlockId(1, 10, 10) == block::PORTAL &&
          w->getBlockId(2, 12, 10) == block::PORTAL,
          "interior filled with PORTAL");
    CHECK(w->getBlockData(1, 10, 10) == 1, "X-axis meta is 1");

    // ---------- marco en eje Z ----------
    for (int z = 0; z <= 3; ++z)
        for (int y = 9; y <= 13; ++y)
            w->setBlock(30, y, z, (y == 9 || y == 13 || z == 0 || z == 3)
                                      ? block::OBSIDIAN : block::AIR, 0);
    CHECK(portals::tryLightNetherPortal(w, 30, 10, 1),
          "portal lights inside a valid 2x3 Z-frame");
    CHECK(w->getBlockData(30, 10, 1) == 2, "Z-axis meta is 2");

    // ---------- marcos rotos ----------
    World* w2 = makeWorld("flat2", "flat", 0, 999);
    for (int x = 0; x <= 3; ++x)
        for (int y = 9; y <= 13; ++y)
            w2->setBlock(x, y, 10, (x == 0 || x == 3 || y == 9)
                                      ? block::OBSIDIAN : block::AIR, 0); // sin techo
    CHECK(!portals::tryLightNetherPortal(w2, 1, 10, 10),
          "frame without top does NOT light");
    // interior bloqueado
    for (int x = 0; x <= 3; ++x)
        for (int y = 9; y <= 13; ++y)
            w2->setBlock(x + 10, y, 10, (x == 0 || x == 3 || y == 9 || y == 13)
                                            ? block::OBSIDIAN : block::AIR, 0);
    w2->setBlock(12, 11, 10, block::STONE, 0);
    CHECK(!portals::tryLightNetherPortal(w2, 11, 10, 10),
          "obstructed interior does NOT light");

    // ---------- romper una obsidiana apaga el portal ----------
    int portalCount = 0;
    for (int y = 10; y <= 12; ++y)
        if (w->getBlockId(1, y, 10) == block::PORTAL) ++portalCount;
    CHECK(portalCount == 3, "3 portal blocks before extinguish");
    portals::extinguishPortalAt(w, 0, 10, 10);   // rompió la obsidiana lateral
    portalCount = 0;
    for (int y = 10; y <= 12; ++y)
        for (int x = 0; x <= 3; ++x)
            if (w->getBlockId(x, y, 10) == block::PORTAL) ++portalCount;
    CHECK(portalCount == 0, "no portal blocks after extinguish");

    // ---------- nether: mar de lava + bedrock techo/piso ----------
    World* wn = makeWorld("nether", "nether", 1, 777);
    loadArea(wn, -16, -16, 16, 16);
    CHECK(wn->getBlockId(0, 0, 0) == block::BEDROCK, "nether floor is bedrock");
    CHECK(wn->getBlockId(0, 127, 0) == block::BEDROCK, "nether ceiling is bedrock");
    // Escaneo amplio: una columna puntual puede ser solida por casualidad.
    int lava = 0;
    for (int dx = -12; dx <= 12 && lava == 0; dx += 4)
    for (int dz = -12; dz <= 12 && lava == 0; dz += 4)
        for (int y = 5; y < 31; ++y)
            if (wn->getBlockId(dx, y, dz) == block::FLOWING_LAVA) { ++lava; break; }
    CHECK(lava > 0, "lava sea present below y=32");

    delete w; delete w2; delete wn;
    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL OK", failures);
    return failures ? 1 : 0;
}
