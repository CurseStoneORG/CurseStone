// tools/light_bench.cpp
//
// Benchmark REAL del coste de luz en el HILO PRINCIPAL durante la
// tormenta de generacion de chunks — el campeon de los 22.9 ms avg del
// /status. Replica el patron de produccion: se genera un campo 17x17 de
// chunks (una entrada de jugador con view distance 8 encola ~289),
// cada instalacion encola LightEngine::onChunkAdded (como hace el
// done-callback en el worker), y el hilo principal drena con
// processPending(kPendingBudget=2) — 2 relights de caja 46x46 por tick.
//
// Reporta: ms total de luz, max processPending(2) de un tick (eso es el
// pico del world tick), ticks para drenar, y un hash FNV-1a de TODA la
// luz resultante (sky + block de cada chunk) que es el oraculo
// bit-exacto para las optimizaciones del LightEngine.
//
//   g++ -std=c++20 -O2 -Isrc -o /tmp/light_bench tools/light_bench.cpp \
//       $(ls build/CMakeFiles/CurseStone.dir/src/*.o \
//          build/CMakeFiles/CurseStone.dir/src/**/*.o \
//          | grep -v "src/CurseStone.cpp.o") -lz -ldl -pthread
//   /tmp/light_bench [lado en chunks, default 17]
#include "Server.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"

#include <chrono>
#include <cmath>
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
    std::string root = "/tmp/cs_light_bench";
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

static uint64_t fnv(const uint8_t* p, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

int main(int argc, char** argv) {
    const int side = argc > 1 ? atoi(argv[1]) : 17;
    const int64_t seed = 1337;

    World* w = makeWorld("bench", seed);

    // --- fase 1: generar el campo (lo que hacen los workers) ----------
    const double g0 = now_ms();
    for (int cx = -side / 2; cx < side - side / 2; ++cx)
        for (int cz = -side / 2; cz < side - side / 2; ++cz)
            w->chunks().getChunk(cx, cz);
    const double gen_ms = now_ms() - g0;

    // getChunk (via setChunk -> on_chunk_added_) ya encolo luz para
    // todos los chunks instalados. Repite la senal por si algun chunk
    // no la dispara, sin duplicar (onChunkAdded deduplica).
    for (int cx = -side / 2; cx < side - side / 2; ++cx)
        for (int cz = -side / 2; cz < side - side / 2; ++cz)
            w->onChunkAdded(cx, cz);

    // --- fase 2: drenar como el hilo principal (2 por tick) -----------
    size_t pending0 = 0;
    {
        // estimar la cola inicial para el reporte
        pending0 = side * side;
    }
    double light_total = 0, light_max = 0, light_sum_sq = 0;
    size_t ticks = 0, done = 0;
    while (w->hasPendingLight()) {
        const double a = now_ms();
        const size_t n = w->drainLightPending(LightEngine::kPendingBudget);
        const double b = now_ms();
        const double dt = b - a;
        light_total += dt;
        if (dt > light_max)
            light_max = dt;
        light_sum_sq += dt * dt;
        ++ticks;
        done += n;
        if (ticks > 100000)
            break; // guard
    }

    // --- hash bit-exacto de la luz resultante -------------------------
    // Lee SIEMPRE via mcp_chunk_get_sky_light / get_block_light: esas
    // accessors definen el contrato observable (seccion ausente = cielo
    // 15 / bloque 0), asi el hash no cambia si una optimizacion deja de
    // materializar secciones que solo guardaban el valor implicito.
    uint64_t h = 1469598103934665603ull;
    for (int cx = -side / 2; cx < side - side / 2; ++cx) {
        for (int cz = -side / 2; cz < side - side / 2; ++cz) {
            mcp_chunk_t* c = w->chunks().getChunk(cx, cz);
            if (!c)
                continue;
            h = fnv((const uint8_t*)&cx, sizeof(cx), h);
            h = fnv((const uint8_t*)&cz, sizeof(cz), h);
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 16; ++x)
                    for (int z = 0; z < 16; ++z) {
                        const uint8_t pair[2] = {mcp_chunk_get_sky_light(c, x, y, z),
                                                 mcp_chunk_get_block_light(c, x, y, z)};
                        h = fnv(pair, 2, h);
                    }
        }
    }

    const double avg = ticks ? light_total / (double)ticks : 0.0;
    const double var = ticks ? light_sum_sq / (double)ticks - avg * avg : 0.0;
    printf("field          : %d chunks (17x17 = vista 8)\n", side * side);
    printf("gen field      : %.1f ms total\n", gen_ms);
    printf("light pending  : ~%zu chunks\n", pending0);
    printf("light total    : %.1f ms en %zu ticks (2 relights/tick)\n", light_total, ticks);
    printf("light per tick : avg %.3f ms | max %.3f ms | p_std %.3f ms\n", avg, light_max,
           var > 0 ? sqrt(var) : 0.0);
    printf("light hash     : %016llx\n", (unsigned long long)h);

    delete w;
    return 0;
}
