// tools/vgen_stream_bench.cpp
// Times the three hot paths of chunk streaming on the vgen generator:
//   1) generation (generate + populate + heightmap + sky fill) per chunk
//   2) mcp_chunk_network_serialize per chunk (main-thread cost per send)
//   3) zlib level-2 compression of the raw payload (BatchPacket path)
// Usage: vgen_stream_bench [N chunks, default 64] [seed, default 1337]
#include "world/generator/vgen/VgenSource.h"
#include "world/generator/Generator.h"
#include "world/format/Chunk.h"
#include "world/format/anvil/AnvilChunkCodec.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>

using namespace cursestone::world;
using namespace cursestone::world::generator;

static double now_us() {
    using namespace std::chrono;
    return duration<double, std::micro>(steady_clock::now().time_since_epoch()).count();
}

static double pct(std::vector<double>& v, double p) {
    if (v.empty())
        return 0;
    size_t idx = (size_t)std::min<double>(v.size() - 1, p * v.size());
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

int main(int argc, char** argv) {
    const int N = argc > 1 ? atoi(argv[1]) : 64;
    const int64_t seed = argc > 2 ? atoll(argv[2]) : 1337;

    auto gen = std::make_shared<VgenSource>(seed);

    std::vector<mcp_chunk_t*> chunks;
    chunks.reserve(N);
    std::vector<double> gen_us, ser_us, zip_us;

    // One-shot (first chunk pays lazy init) — measured separately, like the
    // real server's first boundary crossing after boot.
    {
        mcp_chunk_t* c = mcp_chunk_new(0, 0);
        GenerationContext ctx{seed, 0, 0, nullptr, c};
        double t0 = now_us();
        gen->generate(ctx);
        gen->populate(ctx);
        c->terrain_populated = true;
        mcp_chunk_recalculate_height_map(c);
        mcp_chunk_propagate_sky_light(c);
        double t1 = now_us();
        printf("one-shot chunk (0,0): %.2f ms\n", (t1 - t0) / 1000.0);
        chunks.push_back(c);
    }

    for (int i = 1; i < N; ++i) {
        int cx = (i % 16) - 8, cz = (i / 16) - 2; // spread over 16x4 area
        mcp_chunk_t* c = mcp_chunk_new(cx, cz);
        GenerationContext ctx{seed, cx, cz, nullptr, c};
        double t0 = now_us();
        gen->generate(ctx);
        gen->populate(ctx);
        c->terrain_populated = true;
        mcp_chunk_recalculate_height_map(c);
        mcp_chunk_propagate_sky_light(c);
        double t1 = now_us();
        gen_us.push_back(t1 - t0);
        chunks.push_back(c);
    }

    for (auto* c : chunks) {
        size_t outLen = 0;
        double t0 = now_us();
        uint8_t* buf = mcp_chunk_network_serialize(c, &outLen);
        double t1 = now_us();
        ser_us.push_back(t1 - t0);
        if (!buf) {
            printf("serialize FAILED\n");
            continue;
        }
        // BatchPacket path: compress2 level 2 of the raw payload.
        uLong bound = compressBound(outLen) + 64;
        std::vector<uint8_t> dst(bound);
        uLongf dstLen = bound;
        double t2 = now_us();
        int r = compress2(dst.data(), &dstLen, buf, outLen, 2);
        double t3 = now_us();
        if (r == Z_OK)
            zip_us.push_back(t3 - t2);
        if (c == chunks.front())
            printf("payload: raw %zu B -> zlib-l2 %lu B (%.1f%%)\n", outLen,
                   (unsigned long)dstLen, 100.0 * dstLen / outLen);
        std::free(buf);
    }

    auto report = [](const char* name, std::vector<double>& v) {
        if (v.empty())
            return;
        double sum = 0;
        for (double x : v)
            sum += x;
        std::vector<double> copy = v;
        printf("%-10s n=%3zu  avg=%.3f ms  p50=%.3f  p95=%.3f  max=%.3f\n", name, v.size(),
               sum / v.size() / 1000.0, pct(copy, 0.50) / 1000.0, pct(copy, 0.95) / 1000.0,
               *std::max_element(v.begin(), v.end()) / 1000.0);
    };
    report("generate", gen_us);
    report("serialize", ser_us);
    report("zlib-l2", zip_us);

    // GC unload sweep cost: anvil_chunk_encode() per chunk on the MAIN
    // thread (Anvil::saveChunk main-thread half). This is what a GC pass
    // pays once per unloaded chunk, all inside one tick.
    {
        double enc_total = 0;
        size_t enc_bytes = 0;
        int enc_n = 0;
        for (auto* c : chunks) {
            if (enc_n >= 24)
                break;
            double t0 = now_us();
            std::vector<uint8_t> nbt = anvil_chunk_encode(c);
            double t1 = now_us();
            enc_total += (t1 - t0);
            enc_bytes += nbt.size();
            ++enc_n;
        }
        if (enc_n)
            printf("anvil-encode: avg=%.3f ms/chunk (%zu B avg) -> 255-chunk GC pass ~ %.0f ms\n",
                   enc_total / enc_n / 1000.0, enc_bytes / enc_n,
                   enc_total / enc_n / 1000.0 * 255.0);
    }
    printf("per tick (3 chunks): gen-worst %.2f ms | serialize %.2f ms | compress %.2f ms\n",
           (pct(gen_us, 0.95) * 3) / 1000.0, (pct(ser_us, 0.95) * 3) / 1000.0,
           (pct(zip_us, 0.95) * 3) / 1000.0);
    for (auto* c : chunks)
        mcp_chunk_destroy(c);
    return 0;
}
