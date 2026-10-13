// tools/gen_perf.cpp
//
// Mide el COSTO REAL del generador Normal, con el mismo ruido vanilla.
// Corre uno y N chunks de cada vez y reporta us/car. Antes de tocar nada,
// el "one-shot" (creando TerrainNoise) es el caso real del servidor, y
// despues de optimizar el mismo binario tiene que hacer lo mismo, mas rapido.
//
//   g++ -std=c++20 -O2 -Isrc -o /tmp/gen_perf tools/gen_perf.cpp \
//       src/world/generator/Normal.cpp src/world/generator/TerrainShape.cpp \
//       src/world/generator/noise/*.cpp world/format/Chunk.c ...
//   (ver tools/run_gen_perf.sh)
#include "world/generator/Normal.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"
#include "world/generator/Generator.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using namespace cursestone::world::generator;

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    const int N = atoi(argc > 1 ? argv[1] : "64");
    const int64_t seed = 1337;
    Normal gen(seed);  // el ctor de TerrainNoise corre una vez (calibracion incluida)

    std::vector<mcp_chunk_t*> chunks;
    chunks.reserve(N);
    for (int i = 0; i < N; ++i) {
        const int cx = i & 15, cz = (i >> 4) & 15;
        mcp_chunk_t* c = mcp_chunk_new(cx, cz);
        chunks.push_back(c);
    }

    // warmup: 1 chunk de cada cuadrante del mundo
    if (N > 0) {
        mcp_chunk_t* c = chunks[0];
        GenerationContext ctx{seed, c->x, c->z, nullptr, c};
        double t0 = now_ms();
        gen.generate(ctx);
        gen.populate(ctx);
        double t1 = now_ms();
        printf("warmup 1 chunk : %.1f ms (incluye calibracion)\n", t1 - t0);
    }

    double best = 1e30;
    for (int rep = 0; rep < 3; ++rep) {
        const double t0 = now_ms();
        for (int i = 0; i < N; ++i) {
            mcp_chunk_t* c = chunks[i];
            GenerationContext ctx{seed, c->x, c->z, nullptr, c};
            gen.generate(ctx);
            gen.populate(ctx);
        }
        const double dt = now_ms() - t0;
        if (dt < best) best = dt;
    }

    printf("\n%d chunks : %.1f ms total  ->  %.2f ms/chunk (mejor de 3)\n",
           N, best, best / N);

    for (mcp_chunk_t* c : chunks) mcp_chunk_destroy(c);
    return 0;
}
