// tools/login_decode_harness.cpp
//
// Runs the real LoginPacket::decode() against a byte-exact copy of what a
// MCPE 0.15.10 client puts on the wire, without any sockets. Useful on
// Linux to reproduce memory bugs in the login path in isolation.
//
//   g++ -std=c++20 -Isrc tools/login_decode_harness.cpp <all objects except main.o> -lz -o harness
#include "network/protocol/LoginPacket.h"
#include "network/protocol/PacketPool.h"
#include "network/protocol/Info.h"
#include "utils/Binary.h"

#include <cstdio>
#include <string>
#include <vector>
#include <zlib.h>

using namespace cursestone;

static std::string b64u(const std::string& in) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) |
                     (unsigned char)in[i + 2];
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
        out += T[(v >> 6) & 63];  out += T[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63];
    }
    return out;
}

static void putStr(std::vector<uint8_t>& v, const std::string& s) {
    // NOTE: inside the inflated LoginPacket payload both string lengths are
    // LITTLE-endian (PMMP getLInt), unlike the outer protocol/length fields.
    uint32_t n = (uint32_t)s.size();
    v.push_back((uint8_t)n);  v.push_back((uint8_t)(n >> 8));
    v.push_back((uint8_t)(n >> 16)); v.push_back((uint8_t)(n >> 24));
    v.insert(v.end(), s.begin(), s.end());
}

static void putI32(std::vector<uint8_t>& v, int32_t x) {
    uint32_t u = (uint32_t)x;
    v.push_back((uint8_t)(u >> 24)); v.push_back((uint8_t)(u >> 16));
    v.push_back((uint8_t)(u >> 8));  v.push_back((uint8_t)u);
}

int main() {
    const std::string name = "LinuxTester";
    const std::string chainJwt = b64u("{\"alg\":\"ES384\"}") + "." +
        b64u("{\"displayName\":\"" + name +
             "\",\"identity\":\"11111111-2222-3333-4444-555555555555\",\"extraData\":{}}") +
        ".sig";
    std::string chain = "{\"chain\":[\"" + chainJwt + "\"]}";
    std::string cdataJwt = b64u("{\"alg\":\"ES384\"}") + "." +
        b64u("{\"DisplayName\":\"" + name +
             "\",\"Identity\":\"11111111-2222-3333-4444-555555555555\","
             "\"DeviceModel\":\"Linux\",\"DeviceOS\":7,\"LanguageCode\":\"en_US\","
             "\"SkinId\":\"Standard_Custom\"}") +
        ".sig";

    std::vector<uint8_t> raw;
    putStr(raw, chain);
    putStr(raw, cdataJwt);

    uLongf clen = compressBound((uLong)raw.size());
    std::vector<uint8_t> comp(clen);
    if (compress2(comp.data(), &clen, raw.data(), (uLong)raw.size(), 7) != Z_OK) {
        printf("compress failed\n");
        return 1;
    }
    comp.resize(clen);

    std::vector<uint8_t> body;
    putI32(body, 84);
    putI32(body, (int32_t)comp.size());
    body.insert(body.end(), comp.begin(), comp.end());

    printf("raw=%zu comp=%zu body=%zu\n", raw.size(), comp.size(), body.size());

    auto pkt = network::protocol::PacketPool::get().create(network::protocol::LOGIN_PACKET);
    if (!pkt) { printf("no packet for LOGIN id\n"); return 1; }

    bool ok = pkt->decode(body.data(), body.size());
    printf("decode=%d error='%s'\n", (int)ok,
           ok ? "" : dynamic_cast<network::protocol::LoginPacket*>(pkt.get())->decode_error.c_str());
    if (ok) {
        auto* lp = dynamic_cast<network::protocol::LoginPacket*>(pkt.get());
        printf("username=%s uuid=%s deviceOS=%d edition=%u\n", lp->username.c_str(),
               lp->clientUUID.c_str(), lp->deviceOS, (unsigned)lp->edition);
    }
    printf("SURVIVED\n");
    return 0;
}
