// tools/gen_bench.cpp
//
// Benchmark REAL de generacion de chunks (hilo worker) sobre el codigo
// de produccion: construye un World con el generador vanilla ("normal"),
// genera un campo NxN de chunks con el mismo camino que usa el servidor
// (ChunkManager::getChunk → generate + populate + heightmap + sky light)
// y reporta ms/chunk y chunks/s. Ademas imprime un hash FNV-1a del
// terreno (bloques + data + biomas): ese hash es el oraculo bit-exacto —
// cualquier optimizacion que diga no alterar el terreno tiene que dejar
// el hash IDENTICO.
//
// Compilar contra los objetos reales del build (mismo patron que
// tools/portal_check.cpp), desde la raiz del repo:
//
//   g++ -std=c++20 -O2 -Isrc -o /tmp/gen_bench tools/gen_bench.cpp \
//       $(ls build/CMakeFiles/CurseStone.dir/src/*.o \
//          build/CMakeFiles/CurseStone.dir/src/**/*.o \
//          | grep -v "src/CurseStone.cpp.o") -lz -ldl -pthread
//   /tmp/gen_bench [lado en chunks, default 8]
#include "Server.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace cursestone;
using namespace cursestone::world;
namespace fs = std::filesystem;

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static World* makeWorld(const std::string& name, int64_t seed) {
    std::string root = "/tmp/cs_gen_bench";
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

// FNV-1a 64 sobre los bytes de una seccion (bloques) + nibbles (data).
static uint64_t fnv(const uint8_t* p, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

int main(int argc, char** argv) {
    const int side = argc > 1 ? atoi(argv[1]) : 8;
    const int64_t seed = 1337;

    World* w = makeWorld("bench", seed);

    const double t0 = now_ms();
    std::vector<double> per_chunk;
    per_chunk.reserve(side * side);
    for (int cx = -side / 2; cx < side - side / 2; ++cx) {
        for (int cz = -side / 2; cz < side - side / 2; ++cz) {
            const double a = now_ms();
            mcp_chunk_t* c = w->chunks().getChunk(cx, cz);
            const double b = now_ms();
            if (!c) {
                printf("FAIL: chunk (%d,%d) no se genero\n", cx, cz);
                return 1;
            }
            per_chunk.push_back(b - a);
        }
    }
    const double total = now_ms() - t0;

    // Hash bit-exacto del terreno generado.
    uint64_t h = 1469598103934665603ull;
    for (int cx = -side / 2; cx < side - side / 2; ++cx) {
        for (int cz = -side / 2; cz < side - side / 2; ++cz) {
            mcp_chunk_t* c = w->chunks().getChunk(cx, cz);
            if (!c)
                continue;
            h = fnv((const uint8_t*)&cx, sizeof(cx), h);
            h = fnv((const uint8_t*)&cz, sizeof(cz), h);
            // Lee SIEMPRE via accessors: definen el contrato observable
            // (seccion ausente = aire/data 0), asi el hash solo cambia si
            // cambia el terreno real, no si una optimizacion deja de
            // materializar secciones que solo guardaban ceros implicitos.
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 16; ++x)
                    for (int z = 0; z < 16; ++z) {
                        const uint8_t pair[2] = {mcp_chunk_get_block_id(c, x, y, z),
                                                 mcp_chunk_get_block_data(c, x, y, z)};
                        h = fnv(pair, 2, h);
                    }
            if (c->biomes)
                h = fnv(c->biomes, 256, h);
        }
    }

    double sum = 0, mx = 0;
    for (double t : per_chunk) {
        sum += t;
        if (t > mx)
            mx = t;
    }
    const double avg = sum / (double)per_chunk.size();

    printf("chunks      : %d (%dx%d)\n", side * side, side, side);
    printf("total       : %.1f ms\n", total);
    printf("avg/chunk   : %.3f ms  (p100 %.3f ms)\n", avg, mx);
    printf("throughput  : %.1f chunks/s (1 hilo)\n", per_chunk.size() * 1000.0 / total);
    printf("terrain hash: %016llx\n", (unsigned long long)h);

    delete w;
    return 0;
}
