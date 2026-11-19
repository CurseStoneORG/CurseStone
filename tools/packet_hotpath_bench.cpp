// Micro-benchmark for the new per-packet hot-path cost.
// Build (repo root): g++ -std=c++20 -O2 -w -Isrc -o /tmp/phbench tools/packet_hotpath_bench.cpp src/network/protocol/PacketSanitizer.cpp src/diagnostics/PacketForensics.cpp -lpthread
//   sanitizeOutbound + PacketForensics::recordOut
// over the packets that dominate a real session (MoveEntity broadcasts and
// chunk payloads). Prints ns/packet so the number can be compared against
// the ~500 us/tick budget at 20 TPS.
#include "diagnostics/PacketForensics.h"
#include "network/protocol/PacketSanitizer.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace cursestone;

int main() {
    // A realistic MoveEntity frame: [pid 0x0f] + 23 payload bytes (eid 8 +
    // xyz 12 + 3 angle bytes), exactly what the encoder produces.
    std::vector<uint8_t> move_body(24, 0);
    move_body[0] = 0x0f;
    for (int i = 0; i < 4; ++i)
        move_body[9 + i] = 0x41; // 12.09f BE-ish filler in the x field
    // A realistic 83 KB chunk payload head.
    std::vector<uint8_t> chunk_body(83 * 1024, 0x33);
    chunk_body[0] = 0x34;

    diagnostics::PacketForensics trace;
    const int kIters = 2000000;

    auto t0 = std::chrono::steady_clock::now();
    uint64_t dropped = 0;
    for (int i = 0; i < kIters; ++i) {
        auto v = network::protocol::sanitizeOutbound(move_body[0], move_body.data() + 1,
                                                     move_body.size() - 1);
        dropped += v.violated ? 1 : 0;
    }
    auto t1 = std::chrono::steady_clock::now();
    double ns_move = std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters;

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto v = network::protocol::sanitizeOutbound(chunk_body[0], chunk_body.data() + 1,
                                                     chunk_body.size() - 1);
        dropped += v.violated ? 1 : 0;
    }
    t1 = std::chrono::steady_clock::now();
    double ns_chunk = std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters;

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        trace.recordOut(0x0f, move_body.data() + 1, move_body.size() - 1, false);
    }
    t1 = std::chrono::steady_clock::now();
    double ns_record = std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters;

    printf("sanitize MoveEntity : %8.1f ns/packet\n", ns_move);
    printf("sanitize FullChunk  : %8.1f ns/packet (83 KB payload)\n", ns_chunk);
    printf("forensics recordOut : %8.1f ns/packet\n", ns_record);
    printf("combined worst case : %8.1f ns/packet\n", ns_move + ns_record);
    printf("(dropped=%llu, ring holds %zu records)\n", (unsigned long long)dropped,
           diagnostics::PacketForensics::kOutCapacity);
    return 0;
}
