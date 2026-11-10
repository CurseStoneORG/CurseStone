/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_| |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// Bot.cpp — protocol-84 client implementation.
//
// Wire shapes mirror src/network (RakLib) byte for byte:
//   datagram   : [0x84][u24 LE seq]{ encapsulated records }
//   encap      : [flags][u16 BE bitLen][msgIdx 3LE?][orderIdx 3LE + chan?]
//                [splitCount 4BE + splitId 2BE + splitIndex 4BE?][payload]
//   ACK / NACK : [0xc0|0xa0][u16 BE count]{[0x01][u24 LE] | [0x00][u24 LE min][u24 LE max]}
//   MCPE frame : [0xFE][packet] ; batch: [0x06][u32 BE len][zlib -> {u32 BE subLen][sub]...}
//   login      : [0x01][i32 BE 84][u32 BE outerLen][zlib{u32 LE chainLen][chain][u32 LE cdLen][cdJwt]}
#include "Bot.h"

// The Winsock shim below references std::strerror (POSIX branch) and
// std::string/std::to_string (both branches), so pull those first.
#include <cstring>
#include <string>

#ifdef _WIN32
// The bot is a plain connected-UDP client, so Winsock maps 1:1 to the
// POSIX socket calls below; only startup, close, poll and error text
// need shims.
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using ssize_t = int;
static int cs_close(int fd) { return closesocket((SOCKET)fd); }
static int cs_poll(pollfd* fds, int nfds, int timeout) {
    return WSAPoll(fds, (ULONG)nfds, (INT)timeout);
}
static std::string cs_last_err() {
    return "WSA error " + std::to_string(WSAGetLastError());
}
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define cs_close ::close
#define cs_poll ::poll
static std::string cs_last_err() { return std::strerror(errno); }
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <zlib.h>

namespace csbot {
namespace {

// RakNet offline-message magic (same 16 bytes as src/network/raklib).
const uint8_t RAK_MAGIC[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                               0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};

void putU8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }
void putU16BE(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x & 0xff));
}
void putU24LE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xff));
    v.push_back((uint8_t)((x >> 8) & 0xff));
    v.push_back((uint8_t)((x >> 16) & 0xff));
}
void putU32BE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x & 0xff));
}
void putU32LE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xff));
    v.push_back((uint8_t)((x >> 8) & 0xff));
    v.push_back((uint8_t)((x >> 16) & 0xff));
    v.push_back((uint8_t)((x >> 24) & 0xff));
}
void putI16BE(std::vector<uint8_t>& v, int16_t x) {
    putU16BE(v, (uint16_t)x);
}
void putI32BE(std::vector<uint8_t>& v, int32_t x) {
    putU32BE(v, (uint32_t)x);
}
void putU64BE(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 7; i >= 0; --i)
        v.push_back((uint8_t)((x >> (8 * i)) & 0xff));
}
void putU64LE(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i)
        v.push_back((uint8_t)((x >> (8 * i)) & 0xff));
}
void putF32BE(std::vector<uint8_t>& v, float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    putU32BE(v, x);
}
void putBytes(std::vector<uint8_t>& v, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    v.insert(v.end(), b, b + n);
}
// RakNet string: u16 BE length + raw bytes.
void putStr(std::vector<uint8_t>& v, const std::string& s) {
    putU16BE(v, (uint16_t)s.size());
    putBytes(v, s.data(), s.size());
}

struct Rd {
    const uint8_t* p;
    size_t n, off = 0;
    bool get(void* dst, size_t k) {
        if (off + k > n)
            return false;
        std::memcpy(dst, p + off, k);
        off += k;
        return true;
    }
    uint8_t u8() {
        uint8_t x = 0;
        get(&x, 1);
        return x;
    }
    uint16_t u16be() {
        uint8_t b[2] = {0, 0};
        get(b, 2);
        return (uint16_t)((b[0] << 8) | b[1]);
    }
    uint32_t u24le() {
        uint8_t b[3] = {0, 0, 0};
        get(b, 3);
        return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
    }
    uint32_t u32be() {
        uint8_t b[4] = {0};
        get(b, 4);
        return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    }
    uint32_t u32le() {
        uint8_t b[4] = {0};
        get(b, 4);
        return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    }
    uint64_t u64be() {
        uint64_t x = 0;
        for (int i = 0; i < 8; ++i)
            x = (x << 8) | u8();
        return x;
    }
    uint64_t u64le() {
        uint64_t x = 0;
        for (int i = 7; i >= 0; --i)
            x |= (uint64_t)u8() << (8 * i);
        return x;
    }
    int32_t i32be() { return (int32_t)u32be(); }
};

std::string hexStr(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(H[p[i] >> 4]);
        s.push_back(H[p[i] & 0xf]);
    }
    return s;
}

std::string b64encode(const uint8_t* p, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    s.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8) | p[i + 2];
        s.push_back(T[(v >> 18) & 63]);
        s.push_back(T[(v >> 12) & 63]);
        s.push_back(T[(v >> 6) & 63]);
        s.push_back(T[v & 63]);
    }
    if (n - i == 1) {
        uint32_t v = uint32_t(p[i]) << 16;
        s.push_back(T[(v >> 18) & 63]);
        s.push_back(T[(v >> 12) & 63]);
        s += "==";
    } else if (n - i == 2) {
        uint32_t v = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8);
        s.push_back(T[(v >> 18) & 63]);
        s.push_back(T[(v >> 12) & 63]);
        s.push_back(T[(v >> 6) & 63]);
        s += "=";
    }
    return s;
}

std::string b64urlNoPad(const std::string& s) {
    std::string o = s;
    while (!o.empty() && o.back() == '=')
        o.pop_back();
    for (char& c : o) {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    return o;
}

std::string zlibCompress(const uint8_t* p, size_t n) {
    uLong bound = compressBound((uLong)n) + 64;
    std::vector<uint8_t> tmp(bound);
    uLongf dst = bound;
    if (compress2(tmp.data(), &dst, p, (uLong)n, 6) != Z_OK)
        return {};
    return std::string((const char*)tmp.data(), (size_t)dst);
}

// A minimal stand-in JWT: the 0.15 offline login flow does not verify the
// signature; the server only reads the payload segments.
std::string fakeJwt(const std::string& payloadJson) {
    std::string header = R"({"alg":"HS256","typ":"JWT"})";
    std::string h = b64urlNoPad(b64encode((const uint8_t*)header.data(), header.size()));
    std::string p = b64urlNoPad(b64encode((const uint8_t*)payloadJson.data(), payloadJson.size()));
    std::string sig = "bot";
    std::string s = b64urlNoPad(b64encode((const uint8_t*)sig.data(), sig.size()));
    return h + "." + p + "." + s;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o.push_back('\\');
            o.push_back(c);
        } else if (c >= 0x20) {
            o.push_back(c);
        }
    }
    return o;
}

} // namespace

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------
BotClient::~BotClient() { closeCapture(); if (fd_ >= 0) cs_close(fd_); }

uint64_t BotClient::nowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool BotClient::open(const std::string& host, uint16_t port, std::string& err) {
    host_ = host;
    port_ = port;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        err = cs_last_err();
        return false;
    }
#endif

    std::random_device rd;
    guid_ = ((uint64_t)rd() << 32) ^ rd() ^ nowMs();

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        err = "socket(): " + cs_last_err();
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        err = "cannot resolve host " + host;
        return false;
    }
    sockaddr_in sa{};
    std::memcpy(&sa, res->ai_addr, sizeof(sockaddr_in));
    sa.sin_port = htons(port);
    ::freeaddrinfo(res);
    if (::connect(fd_, (sockaddr*)&sa, sizeof(sa)) != 0) {
        err = "connect(): " + cs_last_err();
        return false;
    }
    return true;
}

void BotClient::fail(const std::string& why) {
    if (lastError_.empty())
        lastError_ = why;
    if (verbose_)
        fprintf(stderr, "[bot] FAIL: %s\n", why.c_str());
}

void BotClient::sendRaw(const std::vector<uint8_t>& bytes) {
    if (fd_ < 0 || bytes.empty())
        return;
    ssize_t n = ::send(fd_, (const char*)bytes.data(), (int)bytes.size(), 0);
    (void)n;
}

bool BotClient::recvOnce(std::vector<uint8_t>& out, int timeoutMs) {
    pollfd p{fd_, POLLIN, 0};
    if (cs_poll(&p, 1, timeoutMs) <= 0)
        return false;
    uint8_t buf[65535];
    ssize_t n = ::recv(fd_, (char*)buf, (int)sizeof(buf), 0);
    if (n <= 0)
        return false;
    out.assign(buf, buf + n);
    return true;
}

// ---------------------------------------------------------------------------
// raknet tx
// ---------------------------------------------------------------------------
void BotClient::buildAndSendDatagram(const std::vector<uint8_t>& payload, uint8_t reliability,
                                     uint8_t channel, bool hasSplit, uint16_t splitId,
                                     uint32_t splitIndex, uint32_t splitCount) {
    std::vector<uint8_t> d;
    d.reserve(payload.size() + 32);
    putU8(d, 0x84);
    putU24LE(d, sendSeq_++ & 0xffffff);
    putU8(d, (uint8_t)((reliability & 0x07) << 5 | (hasSplit ? 0x10 : 0)));
    putU16BE(d, (uint16_t)(payload.size() * 8));
    bool isReliable = (reliability == RELIABILITY_RELIABLE ||
                       reliability == RELIABILITY_RELIABLE_ORDERED);
    bool isOrdered = (reliability == RELIABILITY_RELIABLE_ORDERED);
    if (isReliable)
        putU24LE(d, messageIndex_++);
    if (isOrdered) {
        putU24LE(d, orderIndex_[channel & 0x1f]++);
        putU8(d, channel & 0x1f);
    }
    if (hasSplit) {
        putU32BE(d, splitCount);
        putU16BE(d, splitId);
        putU32BE(d, splitIndex);
    }
    putBytes(d, payload.data(), payload.size());

    if (isReliable) {
        Recovery r;
        r.bytes = d;
        r.timeMs = nowMs();
        recovery_[d[1] | (d[2] << 8) | (d[3] << 16)] = std::move(r);
    }
    sendRaw(d);
}

void BotClient::sendSplit(const std::vector<uint8_t>& payload, uint8_t reliability,
                          uint8_t channel) {
    size_t frag = MAX_FRAG_PAYLOAD;
    uint32_t count = (uint32_t)((payload.size() + frag - 1) / frag);
    uint16_t id = splitIdCounter_++;
    for (uint32_t i = 0; i < count; ++i) {
        size_t off = (size_t)i * frag;
        size_t len = payload.size() - off < frag ? payload.size() - off : frag;
        std::vector<uint8_t> slice(payload.begin() + (long)off, payload.begin() + (long)(off + len));
        buildAndSendDatagram(slice, reliability, channel, true, id, i, count);
    }
}

void BotClient::sendEncap(const std::vector<uint8_t>& payload, uint8_t reliability,
                          uint8_t channel) {
    if (payload.size() > MAX_FRAG_PAYLOAD)
        sendSplit(payload, reliability, channel);
    else
        buildAndSendDatagram(payload, reliability, channel, false, 0, 0, 0);
}

void BotClient::sendOcr1() {
    // [0x05][magic 16][protocol 5][padding] — total 1372 so the server
    // computes MTU = len + 28 = 1400.
    std::vector<uint8_t> d;
    d.reserve(1372);
    putU8(d, 0x05);
    putBytes(d, RAK_MAGIC, 16);
    putU8(d, 5);
    d.resize(1372, 0x00);
    sendRaw(d);
    if (verbose_)
        fprintf(stderr, "[bot] -> OCR1\n");
}

void BotClient::sendOcr2() {
    std::vector<uint8_t> d;
    putU8(d, 0x07);
    putBytes(d, RAK_MAGIC, 16);
    putU8(d, 4); // address family
    uint32_t ipn = 0;
    if (host_ == "127.0.0.1")
        ipn = 0x0100007f;
    else
        inet_pton(AF_INET, host_.c_str(), &ipn);
    putBytes(d, &ipn, 4);
    putU16BE(d, port_);
    putU16BE(d, (uint16_t)(serverMtu_ ? serverMtu_ : BOT_MTU));
    putU64BE(d, guid_);
    sendRaw(d);
    if (verbose_)
        fprintf(stderr, "[bot] -> OCR2\n");
}

void BotClient::sendClientConnect() {
    std::vector<uint8_t> p;
    putU8(p, ID_CLIENT_CONNECT);
    putU64BE(p, guid_);      // client GUID
    putU64BE(p, nowMs());    // sendPingTime
    putU64BE(p, nowMs());    // sendPongTime
    sendEncap(p, RELIABILITY_RELIABLE, 0);
    if (verbose_)
        fprintf(stderr, "[bot] -> CLIENT_CONNECT\n");
}

void BotClient::sendClientHandshake() {
    std::vector<uint8_t> p;
    putU8(p, ID_CLIENT_HANDSHAKE);
    putU8(p, 4); // address family
    uint32_t ipn = 0;
    if (host_ == "127.0.0.1")
        ipn = 0x0100007f;
    else
        inet_pton(AF_INET, host_.c_str(), &ipn);
    putBytes(p, &ipn, 4);
    putU16BE(p, port_);
    // 10 system addresses (real client shape; the server skips them)
    for (int i = 0; i < 10; ++i) {
        putU8(p, 4);
        putU32BE(p, 0);
        putU16BE(p, 0);
    }
    putU64BE(p, nowMs());
    putU64BE(p, nowMs());
    sendEncap(p, RELIABILITY_RELIABLE_ORDERED, 0);
    if (verbose_)
        fprintf(stderr, "[bot] -> CLIENT_HANDSHAKE (session open)\n");
}

void BotClient::sendRakPing() {
    std::vector<uint8_t> p;
    putU8(p, ID_RAK_PING);
    putU64BE(p, nowMs());
    sendEncap(p, RELIABILITY_UNRELIABLE, 0);
}

void BotClient::sendDisconnect() {
    std::vector<uint8_t> p;
    putU8(p, ID_CLIENT_DISCONNECT);
    sendEncap(p, RELIABILITY_UNRELIABLE, 0);
}

void BotClient::flushAcks() {
    uint64_t now = nowMs();
    if (ackQueue_.empty() && nackQueue_.empty())
        return;
    if (now - lastAckFlushMs_ < 50)
        return;
    lastAckFlushMs_ = now;

    // collapse acks into ranges
    std::vector<uint32_t> a = ackQueue_;
    std::sort(a.begin(), a.end());
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (uint32_t v : a) {
        if (!ranges.empty() && v == ranges.back().second + 1)
            ranges.back().second = v;
        else if (!ranges.empty() && v == ranges.back().second)
            continue;
        else
            ranges.push_back({v, v});
    }
    ackQueue_.clear();

    if (!ranges.empty()) {
        std::vector<uint8_t> d;
        putU8(d, ID_ACK);
        putU16BE(d, (uint16_t)ranges.size());
        for (auto& r : ranges) {
            if (r.first == r.second) {
                putU8(d, 0x01);
                putU24LE(d, r.first);
            } else {
                putU8(d, 0x00);
                putU24LE(d, r.first);
                putU24LE(d, r.second);
            }
        }
        sendRaw(d);
    }

    if (!nackQueue_.empty()) {
        std::vector<uint8_t> d;
        putU8(d, ID_NACK);
        putU16BE(d, (uint16_t)nackQueue_.size());
        for (auto& r : nackQueue_) {
            if (r.first == r.second) {
                putU8(d, 0x01);
                putU24LE(d, r.first);
            } else {
                putU8(d, 0x00);
                putU24LE(d, r.first);
                putU24LE(d, r.second);
            }
        }
        nackQueue_.clear();
        sendRaw(d);
    }
}

void BotClient::retransmitStale() {
    uint64_t now = nowMs();
    for (auto& kv : recovery_) {
        Recovery& r = kv.second;
        if (now - r.timeMs > 2000 && r.retries < 10) {
            sendRaw(r.bytes);
            r.timeMs = now;
            r.retries++;
        }
    }
}

// ---------------------------------------------------------------------------
// raknet rx
// ---------------------------------------------------------------------------
void BotClient::handleAck(const std::vector<uint8_t>& d, bool isNack) {
    if (d.size() < 3)
        return;
    Rd r{d.data(), d.size(), 1};
    uint16_t count = r.u16be();
    for (uint16_t i = 0; i < count && r.off + 1 <= d.size(); ++i) {
        uint8_t kind = r.u8();
        if (kind == 0x01) {
            uint32_t v = r.u24le();
            if (isNack)
                nackResend(v, v);
            else
                recovery_.erase(v);
        } else if (kind == 0x00) {
            uint32_t lo = r.u24le(), hi = r.u24le();
            if (isNack)
                nackResend(lo, hi);
            else
                for (uint32_t v = lo; v != hi; v = (v + 1) & 0xffffff)
                    recovery_.erase(v);
            if (!isNack)
                recovery_.erase(hi);
        }
    }
}

void BotClient::nackResend(uint32_t lo, uint32_t hi) {
    // NACKs reference OUR send sequence numbers: resend those datagrams now.
    for (uint32_t v = lo;; v = (v + 1) & 0xffffff) {
        auto it = recovery_.find(v);
        if (it != recovery_.end()) {
            sendRaw(it->second.bytes);
            it->second.timeMs = nowMs();
            it->second.retries++;
        }
        if (v == hi)
            break;
    }
}

void BotClient::handleOffline(const std::vector<uint8_t>& d) {
    uint8_t pid = d[0];
    if (pid == ID_OPEN_CONNECTION_REPLY_1) {
        // layout: [0x06][magic 16][serverGuid u64 BE][security u8][mtu u16 BE]
        if (d.size() < 28)
            return;
        Rd r{d.data(), d.size(), 17};
        serverGuid_ = r.u64be();
        uint8_t sec = r.u8();
        serverMtu_ = r.u16be();
        if (sec) {
            fail("server demanded security (unsupported)");
            state_ = State::Closed;
            return;
        }
        state_ = State::Connecting2;
        sendOcr2();
    } else if (pid == ID_OPEN_CONNECTION_REPLY_2) {
        state_ = State::Connecting2;
        sendClientConnect();
    } else if (pid == 0x1c) {
        if (verbose_)
            fprintf(stderr, "[bot] <- UNCONNECTED_PONG (server alive)\n");
    }
}

void BotClient::deliverEncap(std::vector<uint8_t> payload) {
    if (payload.empty())
        return;
    uint8_t pid = payload[0];

    if (state_ == State::Connecting2) {
        if (pid == ID_SERVER_HANDSHAKE) {
            if (verbose_)
                fprintf(stderr, "[bot] <- SERVER_HANDSHAKE\n");
            sendClientHandshake();
            state_ = State::Connected;
            capture("raknet_connected", nullptr, 0);
        } else if (pid == ID_RAK_PONG) {
            // ignore
        }
        return;
    }

    if (state_ != State::Connected)
        return;

    if (pid == ID_RAK_PING && payload.size() >= 9) {
        std::vector<uint8_t> pong;
        putU8(pong, ID_RAK_PONG);
        putBytes(pong, payload.data() + 1, 8);
        sendEncap(pong, RELIABILITY_UNRELIABLE, 0);
        return;
    }
    if (pid == ID_RAK_PONG)
        return;

    // MCPE frame: real clients and the server prefix 0xFE to game packets.
    if (pid == 0xFE && payload.size() > 1)
        payload.erase(payload.begin());
    handleMcpeFrame(std::move(payload));
}

void BotClient::handleDatagram(const std::vector<uint8_t>& dgram) {
    if (dgram.empty())
        return;
    uint8_t id = dgram[0];

    if (id == ID_ACK || id == ID_NACK) {
        handleAck(dgram, id == ID_NACK);
        return;
    }
    if (id == ID_OPEN_CONNECTION_REPLY_1 || id == ID_OPEN_CONNECTION_REPLY_2 || id == 0x1c) {
        handleOffline(dgram);
        return;
    }
    if ((id & 0xf0) != 0x80)
        return; // anything else is not a data datagram

    if (dgram.size() < 4)
        return;
    uint32_t seq = dgram[1] | (dgram[2] << 8) | (dgram[3] << 16);

    // ACK this datagram back to the server.
    if (ackQueue_.empty() || ackQueue_.back() != seq)
        ackQueue_.push_back(seq);

    // duplicate / gap detection (server retransmits keep the same seq)
    if (lastRecvSeq_ >= 0) {
        uint32_t last = (uint32_t)lastRecvSeq_;
        if (seq == last)
            return; // duplicate
        if (seq > last && seq - last > 1 && seq - last < 4096)
            nackQueue_.push_back({last + 1, seq - 1});
    }
    lastRecvSeq_ = (int64_t)seq;

    size_t off = 4;
    while (off < dgram.size()) {
        if (off + 3 > dgram.size())
            break;
        uint8_t flags = dgram[off++];
        uint8_t reliability = (flags >> 5) & 0x07;
        bool hasSplit = (flags & 0x10) != 0;
        bool isReliable = (reliability >= 2 && reliability <= 4) || reliability == 6 ||
                          reliability == 7;
        bool isOrdered = (reliability == 3 || reliability == 7);
        bool isSequenced = (reliability == 1 || reliability == 4);
        uint16_t bitLen = (uint16_t)((dgram[off] << 8) | dgram[off + 1]);
        off += 2;
        size_t plen = ((size_t)bitLen + 7) / 8;

        uint8_t channel = 0;
        uint32_t orderIndex = 0;
        if (isReliable)
            off += 3; // messageIndex
        if (isSequenced)
            off += 3; // sequenceIndex
        if (isOrdered) {
            if (off + 4 > dgram.size())
                break;
            orderIndex = (uint32_t)dgram[off] | ((uint32_t)dgram[off + 1] << 8) |
                         ((uint32_t)dgram[off + 2] << 16);
            off += 3;
            channel = dgram[off++] & 0x1f;
        }
        uint32_t splitCount = 0, splitIndex = 0;
        uint16_t splitId = 0;
        if (hasSplit) {
            if (off + 10 > dgram.size())
                break;
            splitCount = ((uint32_t)dgram[off] << 24) | ((uint32_t)dgram[off + 1] << 16) |
                         ((uint32_t)dgram[off + 2] << 8) | dgram[off + 3];
            off += 4;
            splitId = (uint16_t)((dgram[off] << 8) | dgram[off + 1]);
            off += 2;
            splitIndex = ((uint32_t)dgram[off] << 24) | ((uint32_t)dgram[off + 1] << 16) |
                         ((uint32_t)dgram[off + 2] << 8) | dgram[off + 3];
            off += 4;
        }
        if (off + plen > dgram.size())
            break;

        std::vector<uint8_t> payload(dgram.begin() + (long)off,
                                     dgram.begin() + (long)(off + plen));
        off += plen;

        if (hasSplit && splitCount > 1) {
            Split& s = splits_[splitId];
            if (s.parts.empty() && splitCount <= 4096) {
                s.count = splitCount;
                s.parts.resize(splitCount);
            }
            if (splitIndex < s.count && s.parts[splitIndex].empty()) {
                s.parts[splitIndex] = std::move(payload);
                s.got++;
            }
            if (s.got == s.count) {
                std::vector<uint8_t> whole;
                size_t total = 0;
                for (auto& part : s.parts)
                    total += part.size();
                whole.reserve(total);
                for (auto& part : s.parts)
                    putBytes(whole, part.data(), part.size());
                splits_.erase(splitId);
                enqueueOrdered(channel, orderIndex, std::move(whole), isOrdered);
            }
        } else {
            enqueueOrdered(channel, orderIndex, std::move(payload), isOrdered);
        }
    }
}

// Ordered delivery: channel-0 records (login/spawn) must reach the MCPE
// layer in the order the server sent them; channel-1 traffic (chunks,
// movement) is delivered immediately.
void BotClient::enqueueOrdered(uint8_t channel, uint32_t orderIndex, std::vector<uint8_t> payload,
                               bool isOrdered) {
    if (!isOrdered) {
        deliverEncap(std::move(payload));
        return;
    }
    if (!startedOrder_[channel]) {
        startedOrder_[channel] = true;
        nextOrder_[channel] = orderIndex;
    }
    if (orderIndex < nextOrder_[channel])
        return; // duplicate of an already-delivered record
    hold_[channel][orderIndex] = std::move(payload);
    // drain in order
    while (true) {
        auto it = hold_[channel].find(nextOrder_[channel]);
        if (it == hold_[channel].end())
            break;
        deliverEncap(std::move(it->second));
        hold_[channel].erase(it);
        nextOrder_[channel]++;
    }
}

// ---------------------------------------------------------------------------
// mcpe
// ---------------------------------------------------------------------------
void BotClient::handleMcpeFrame(std::vector<uint8_t> frame) {
    if (frame.empty())
        return;
    if (frame[0] == MCPE_BATCH) {
        handleBatchBody(frame.data() + 1, frame.size() - 1);
        return;
    }
    handleMcpePacket(frame);
}

void BotClient::handleBatchBody(const uint8_t* body, size_t len) {
    if (len < 4)
        return;
    uint32_t compLen = ((uint32_t)body[0] << 24) | ((uint32_t)body[1] << 16) |
                       ((uint32_t)body[2] << 8) | body[3];
    if (compLen > len - 4)
        return;

    static const size_t kMaxInflated = 8u << 20;
    std::vector<uint8_t> dst(kMaxInflated);
    uLongf dstLen = kMaxInflated;
    int rc = uncompress(dst.data(), &dstLen, body + 4, compLen);
    if (rc != Z_OK) {
        if (verbose_)
            fprintf(stderr, "[bot] batch inflate failed rc=%d (compLen=%u)\n", rc,
                    (unsigned)compLen);
        return;
    }

    size_t off = 0;
    while (off + 4 <= dstLen) {
        uint32_t subLen = ((uint32_t)dst[off] << 24) | ((uint32_t)dst[off + 1] << 16) |
                          ((uint32_t)dst[off + 2] << 8) | dst[off + 3];
        off += 4;
        if (off + subLen > dstLen)
            break;
        std::vector<uint8_t> sub(dst.begin() + (long)off, dst.begin() + (long)(off + subLen));
        off += subLen;
        handleMcpePacket(sub);
    }
}

void BotClient::handleMcpePacket(const std::vector<uint8_t>& pkt) {
    if (pkt.empty())
        return;
    uint8_t pid = pkt[0];
    packetCounts_[pid]++;
    capture("packet", &pkt, pid);

    switch (pid) {
    case MCPE_PLAY_STATUS: {
        if (pkt.size() < 5)
            return;
        int32_t status = (int32_t)(((uint32_t)pkt[1] << 24) | ((uint32_t)pkt[2] << 16) |
                                   ((uint32_t)pkt[3] << 8) | pkt[4]);
        if (status == 0 && !loginAccepted_) {
            loginAccepted_ = true;
            if (verbose_)
                fprintf(stderr, "[bot] <- PlayStatus LOGIN_SUCCESS\n");
            capture("play_status", &pkt, pid);
        } else if (status == 3) {
            if (!spawned_)
                capture("player_spawn", &pkt, pid);
            spawned_ = true;
            if (verbose_)
                fprintf(stderr, "[bot] <- PlayStatus PLAYER_SPAWN\n");
        } else {
            if (verbose_)
                fprintf(stderr, "[bot] <- PlayStatus status=%d\n", (int)status);
            capture("play_status", &pkt, pid);
            if (status == 1 || status == 2)
                fail("login rejected: status " + std::to_string(status));
        }
        return;
    }
    case MCPE_FULL_CHUNK: {
        chunksReceived_++;
        chunkBytes_ += pkt.size();
        // Furnace scenario: scan the column-format block array for a
        // natural furnace block (61/62 — village houses place them).
        // Wire layout (FullChunkDataPacket::encode): pid + chunkX i32 BE
        // + chunkZ i32 BE + order u8 + dataLen i32 BE + data. First hit
        // wins and becomes the touch target.
        if (!furnaceFound_ && pkt.size() > 14 + 32768) {
            int32_t cx = (int32_t)(((uint32_t)pkt[1] << 24) | ((uint32_t)pkt[2] << 16) |
                                   ((uint32_t)pkt[3] << 8) | (uint32_t)pkt[4]);
            int32_t cz = (int32_t)(((uint32_t)pkt[5] << 24) | ((uint32_t)pkt[6] << 16) |
                                   ((uint32_t)pkt[7] << 8) | (uint32_t)pkt[8]);
            std::vector<uint8_t> payload(pkt.begin() + 14, pkt.end());
            scanChunkForFurnace(cx, cz, payload);
        }
        return;
    }
    case MCPE_CONTAINER_OPEN: {
        // Wire: windowid u8, type u8, slots i16 BE, x/y/z i32 BE.
        if (pkt.size() >= 17) {
            containerWindowId_ = pkt[1];
            containerOpens_++;
            // The 0.15.10 client aborts inside its furnace screen on every
            // type=2 open (bisected on a real client) — the server must
            // open furnaces as generic container windows (type 0).
            if (pkt[2] == 2)
                fail("server sent a type=2 ContainerOpen (furnace window) — "
                     "the 0.15.10 client aborts in its furnace screen");
            int32_t ox = (int32_t)(((uint32_t)pkt[5] << 24) | ((uint32_t)pkt[6] << 16) |
                                   ((uint32_t)pkt[7] << 8) | (uint32_t)pkt[8]);
            int32_t oy = (int32_t)(((uint32_t)pkt[9] << 24) | ((uint32_t)pkt[10] << 16) |
                                   ((uint32_t)pkt[11] << 8) | (uint32_t)pkt[12]);
            int32_t oz = (int32_t)(((uint32_t)pkt[13] << 24) | ((uint32_t)pkt[14] << 16) |
                                   ((uint32_t)pkt[15] << 8) | (uint32_t)pkt[16]);
            if (verbose_)
                fprintf(stderr,
                        "[bot] <- ContainerOpen window=%u type=%u at %d,%d,%d\n",
                        (unsigned)pkt[1], (unsigned)pkt[2], ox, oy, oz);
            if (ox != furnaceX_ || oy != furnaceY_ || oz != furnaceZ_)
                fprintf(stderr, "[bot] WARNING ContainerOpen coords mismatch: "
                                "asked %d,%d,%d got %d,%d,%d\n",
                        furnaceX_, furnaceY_, furnaceZ_, ox, oy, oz);
        }
        return;
    }
    case MCPE_CONTAINER_CLOSE: {
        if (verbose_)
            fprintf(stderr, "[bot] <- ContainerClose window=%u\n", (unsigned)pkt[1]);
        return;
    }
    case MCPE_BLOCK_ENTITY_DATA: {
        blockEntityDatas_++;
        if (verbose_)
            fprintf(stderr, "[bot] <- BlockEntityData (%zu bytes)\n", pkt.size());
        return;
    }
    case MCPE_START_GAME: {
        startGameCount_++;
        if (startGameCount_ == 1 && verbose_)
            fprintf(stderr, "[bot] <- StartGame (%zu bytes)\n", pkt.size());
        // First StartGame also tells the bot where the server actually
        // placed it: [pid][seed i32][dim u8][gen i32][gm i32][eid i64]
        // [spawnX i32][spawnY i32][spawnZ i32][x f32][y f32][z f32].
        // Seeding moveX_/Y_/Z_ from it keeps the long march compatible
        // with the server's 20-block move cap (it measures from ITS
        // tracked position, not ours).
        if (startGameCount_ == 1 && pkt.size() >= 46 && !gotoDone_) {
            uint32_t xr = ((uint32_t)pkt[34] << 24) | ((uint32_t)pkt[35] << 16) |
                          ((uint32_t)pkt[36] << 8) | (uint32_t)pkt[37];
            uint32_t yr = ((uint32_t)pkt[38] << 24) | ((uint32_t)pkt[39] << 16) |
                          ((uint32_t)pkt[40] << 8) | (uint32_t)pkt[41];
            uint32_t zr = ((uint32_t)pkt[42] << 24) | ((uint32_t)pkt[43] << 16) |
                          ((uint32_t)pkt[44] << 8) | (uint32_t)pkt[45];
            float fx, fy, fz;
            std::memcpy(&fx, &xr, 4);
            std::memcpy(&fy, &yr, 4);
            std::memcpy(&fz, &zr, 4);
            moveX_ = (double)fx;
            moveY_ = (double)fy;
            moveZ_ = (double)fz;
            if (verbose_)
                fprintf(stderr, "[bot] server spawn position: %.1f,%.1f,%.1f\n", fx, fy, fz);
        }
        return;
    }
    case MCPE_CHUNK_RADIUS_UPDATED: {
        if (verbose_)
            fprintf(stderr, "[bot] <- ChunkRadiusUpdated\n");
        return;
    }
    case MCPE_TEXT: {
        if (verbose_)
            fprintf(stderr, "[bot] <- Text (%zu bytes)\n", pkt.size());
        return;
    }
    default:
        return;
    }
}

void BotClient::sendMcpe(const std::vector<uint8_t>& pkt, bool ordered) {
    std::vector<uint8_t> frame;
    frame.reserve(pkt.size() + 1);
    putU8(frame, 0xFE);
    putBytes(frame, pkt.data(), pkt.size());
    sendEncap(frame, ordered ? RELIABILITY_RELIABLE_ORDERED : RELIABILITY_RELIABLE, 0);
}

void BotClient::doLogin(const std::string& username) {
    // clientData JWT payload (0.15 field names as the server reads them)
    static const uint8_t skin[64 * 32 * 4] = {0};
    std::string skinB64 = b64encode(skin, sizeof(skin));
    std::string uuid = "123e4567-e89b-12d3-a456-426614174000";
    std::string cd = "{";
    cd += "\"DisplayName\":\"" + jsonEscape(username) + "\",";
    cd += "\"Identity\":\"" + uuid + "\",";
    cd += "\"SkinId\":\"Standard_Custom\",";
    cd += "\"SkinData\":\"" + skinB64 + "\",";
    cd += "\"LanguageCode\":\"en_US\",";
    cd += "\"DeviceModel\":\"CurseStoneBot\",";
    cd += "\"DeviceOS\":1,";
    cd += "\"ClientRandomId\":424242";
    cd += "}";

    std::string chainPayload = "{\"displayName\":\"" + jsonEscape(username) +
                               "\",\"identity\":\"" + uuid + "\",\"identityPublicKey\":\"\"}";
    std::string chainJwt = fakeJwt(chainPayload);
    std::string chain = "{\"chain\":[\"" + chainJwt + "\"]}";
    std::string cdJwt = fakeJwt(cd);

    std::string inner;
    {
        std::vector<uint8_t> b;
        putU32LE(b, (uint32_t)chain.size());
        putBytes(b, chain.data(), chain.size());
        putU32LE(b, (uint32_t)cdJwt.size());
        putBytes(b, cdJwt.data(), cdJwt.size());
        inner.assign((const char*)b.data(), b.size());
    }
    std::string comp = zlibCompress((const uint8_t*)inner.data(), inner.size());
    if (comp.empty()) {
        fail("zlib compress failed building login");
        return;
    }
    std::vector<uint8_t> pkt;
    putU8(pkt, MCPE_LOGIN);
    putU32BE(pkt, 84); // protocol
    putU32BE(pkt, (uint32_t)comp.size());
    putBytes(pkt, comp.data(), comp.size());

    sendMcpe(pkt, true);
    if (verbose_)
        fprintf(stderr, "[bot] -> Login (username=%s, %zu bytes)\n", username.c_str(),
                pkt.size());
    capture("login_sent", nullptr, 0);
}

void BotClient::sendRequestChunkRadius(int32_t radius) {
    std::vector<uint8_t> pkt;
    putU8(pkt, MCPE_REQUEST_CHUNK_RADIUS);
    putU32BE(pkt, (uint32_t)radius);
    sendMcpe(pkt, true);
    radiusSent_ = true;
    if (verbose_)
        fprintf(stderr, "[bot] -> RequestChunkRadius(%d)\n", (int)radius);
}

void BotClient::sendMovePlayer(double x, double y, double z, float yaw, float pitch,
                               bool onGround) {
    std::vector<uint8_t> pkt;
    putU8(pkt, MCPE_MOVE_PLAYER);
    putU64BE(pkt, 1); // eid (server overrides with its own entity id)
    putF32BE(pkt, (float)x);
    putF32BE(pkt, (float)y);
    putF32BE(pkt, (float)z);
    putF32BE(pkt, yaw);
    putF32BE(pkt, yaw);   // bodyYaw
    putF32BE(pkt, pitch);
    putU8(pkt, 0);        // mode: normal
    putU8(pkt, onGround ? 1 : 0);
    sendMcpe(pkt, false);
}

void BotClient::sendChat(const std::string& message) {
    std::vector<uint8_t> pkt;
    putU8(pkt, MCPE_TEXT);
    putU8(pkt, 1); // TYPE_RAW
    putStr(pkt, message);
    sendMcpe(pkt, true);
    chatSent_++;
}

// Malformed-traffic scenarios: every kind runs on a fresh connection from
// main.cpp; here we only emit the bad bytes once the RakNet session is open.
void BotClient::sendMalformed(int kind) {
    std::vector<uint8_t> raw;
    switch (kind) {
    case 0: { // batch with corrupt zlib
        std::vector<uint8_t> pkt;
        putU8(pkt, MCPE_BATCH);
        putU32BE(pkt, 16);
        for (int i = 0; i < 16; ++i)
            putU8(pkt, (uint8_t)(0xa0 + i));
        putU8(raw, 0xFE);
        putBytes(raw, pkt.data(), pkt.size());
        break;
    }
    case 1: { // batch with a sub-packet length that overruns the payload
        std::vector<uint8_t> pkt;
        putU8(pkt, MCPE_BATCH);
        putU32BE(pkt, 4);
        putU8(pkt, 0x78); // zlib header, then nothing usable
        putU8(pkt, 0x9c);
        putU8(pkt, 0x00);
        putU8(pkt, 0x00);
        putU8(raw, 0xFE);
        putBytes(raw, pkt.data(), pkt.size());
        break;
    }
    case 2: { // login with wrong protocol number
        std::vector<uint8_t> pkt;
        putU8(pkt, MCPE_LOGIN);
        putU32BE(pkt, 83); // not 84
        putU32BE(pkt, 0);
        putU8(raw, 0xFE);
        putBytes(raw, pkt.data(), pkt.size());
        break;
    }
    case 3: { // login whose zlib payload is garbage
        std::vector<uint8_t> pkt;
        putU8(pkt, MCPE_LOGIN);
        putU32BE(pkt, 84);
        putU32BE(pkt, 8);
        for (int i = 0; i < 8; ++i)
            putU8(pkt, (uint8_t)(0xf0 + i));
        putU8(raw, 0xFE);
        putBytes(raw, pkt.data(), pkt.size());
        break;
    }
    case 4: { // unknown MCPE packet id inside a valid frame
        std::vector<uint8_t> pkt;
        putU8(pkt, 0x7f); // not a real packet id
        for (int i = 0; i < 16; ++i)
            putU8(pkt, (uint8_t)i);
        putU8(raw, 0xFE);
        putBytes(raw, pkt.data(), pkt.size());
        break;
    }
    case 5: { // TextPacket with an oversized string length prefix
        std::vector<uint8_t> pkt;
        putU8(pkt, MCPE_TEXT);
        putU8(pkt, 1);
        putU16BE(pkt, 0xf000); // length lies: 61kB of "string" promised
        putStr(pkt, "short");
        putU8(raw, 0xFE);
        putBytes(raw, pkt.data(), pkt.size());
        break;
    }
    default:
        return;
    }
    sendEncap(raw, RELIABILITY_RELIABLE_ORDERED, 0);
    malformedSent_++;
    capture("malformed_sent", nullptr, 0);
}

// ---------------------------------------------------------------------------
// capture
// ---------------------------------------------------------------------------
void BotClient::openCapture(const std::string& path) {
    if (path.empty())
        return;
    cap_ = std::fopen(path.c_str(), "w");
    captureStartMs_ = nowMs();
}

void BotClient::closeCapture() {
    if (cap_) {
        std::fclose(cap_);
        cap_ = nullptr;
    }
}

void BotClient::capture(const std::string& event, const std::vector<uint8_t>* data, uint8_t pid) {
    if (!cap_)
        return;
    std::fprintf(cap_, "{\"t\":%llu,\"ev\":\"%s\"", (unsigned long long)(nowMs() - captureStartMs_),
                 event.c_str());
    if (data) {
        std::fprintf(cap_, ",\"pid\":%u,\"len\":%zu", (unsigned)pid, data->size());
        // Small packets are captured whole; very large ones (chunk payloads)
        // only the first few are dumped, to keep the file manageable.
        if (data->size() <= 4096 || bigPacketCaptured_ < 3) {
            std::fprintf(cap_, ",\"hex\":\"%s\"", hexStr(data->data(), data->size()).c_str());
            if (data->size() > 4096)
                bigPacketCaptured_++;
        }
    }
    std::fprintf(cap_, "}\n");
    std::fflush(cap_);
}

// Furnace-scenario senders — the exact packets a real 0.15.10 client
// emits when the player taps a furnace block and then closes the window.
void BotClient::sendUseItem(int32_t x, int32_t y, int32_t z, uint8_t face, double px,
                            double py, double pz) {
    std::vector<uint8_t> pkt;
    putU8(pkt, MCPE_USE_ITEM);
    putI32BE(pkt, x);
    putI32BE(pkt, y);
    putI32BE(pkt, z);
    putU8(pkt, face);
    putF32BE(pkt, 0.5f);
    putF32BE(pkt, 1.0f);
    putF32BE(pkt, 0.5f);
    putF32BE(pkt, (float)px);
    putF32BE(pkt, (float)py);
    putF32BE(pkt, (float)pz);
    putI32BE(pkt, 0);  // hotbar slot 0
    putI16BE(pkt, 0);  // empty held item (Slot ends after id 0)
    sendMcpe(pkt, true);
    useItemSent_++;
}

void BotClient::sendContainerClose(uint8_t windowid) {
    std::vector<uint8_t> pkt;
    putU8(pkt, MCPE_CONTAINER_CLOSE);
    putU8(pkt, windowid);
    sendMcpe(pkt, true);
    closeSent_++;
}

// Column-format scan: blocks live in the first 32768 bytes of the
// FullChunkData payload at index (x<<11)|(z<<7)|y (y = 0..127).
void BotClient::scanChunkForFurnace(int32_t cx, int32_t cz,
                                    const std::vector<uint8_t>& payload) {
    const size_t kBlocks = 32768;
    if (payload.size() < kBlocks)
        return;
    for (size_t i = 0; i < kBlocks; ++i) {
        uint8_t id = payload[i];
        if (id != BLOCK_FURNACE && id != BLOCK_BURNING_FURNACE)
            continue;
        int x = (int)(i >> 11);
        int z = (int)((i >> 7) & 0xF);
        int y = (int)(i & 0x7F);
        furnaceX_ = cx * 16 + x;
        furnaceY_ = y;
        furnaceZ_ = cz * 16 + z;
        furnaceFound_ = true;
        return;
    }
}

// Furnace state machine — walk to the natural furnace, tap it (twice,
// exercising both the create-tile and reuse-tile server paths) and
// close the window between taps, exactly like a human reproducing the
// village-furnace client crash.
void BotClient::furnaceTick(uint64_t now) {
    if (furnacePhase_ == 6)
        return;
    // Long-distance march: the spawn area may not contain a village. The
    // server caps each MovePlayer at 20 blocks, so hop 15-block steps
    // toward the target until it is inside the interaction range — chunks
    // (and the furnace scan) stream as we go.
    if (goto_ && !gotoDone_) {
        if (now - lastFurnaceMs_ > 120) {
            lastFurnaceMs_ = now;
            double dx = gotoX_ - moveX_, dz = gotoZ_ - moveZ_;
            double dist = std::sqrt(dx * dx + dz * dz);
            if (dist <= 14.0) {
                gotoDone_ = true;
                phaseStartMs_ = now; // restart the no-furnace deadline here
                lastFurnaceMs_ = 0;
                if (verbose_)
                    fprintf(stderr, "[bot] arrived at %.0f,%.0f after %u hops\n", gotoX_,
                            gotoZ_, gotoSteps_);
                return;
            }
            double step = 15.0 / dist;
            moveX_ += dx * step;
            moveZ_ += dz * step;
            ++gotoSteps_;
            sendMovePlayer(moveX_, moveY_, moveZ_, 0.0f, 0.0f, true);
        }
        return;
    }
    switch (furnacePhase_) {
    case 0: { // wait for chunks; declare failure honestly when none found
        if (phaseStartMs_ == 0)
            phaseStartMs_ = now;
        if (furnaceFound_) {
            furnacePhase_ = 1;
            phaseStartMs_ = now;
            lastFurnaceMs_ = 0;
            fprintf(stderr, "[bot] natural furnace at %d,%d,%d\n", furnaceX_, furnaceY_,
                    furnaceZ_);
        } else if (now - phaseStartMs_ > 25000) {
            noFurnace_ = true;
            fail("no natural furnace block in " + std::to_string(chunksReceived_) +
                 " streamed chunks (try a seed with a village near spawn)");
            furnacePhase_ = 6;
        }
        break;
    }
    case 1: { // walk: stand ON the furnace cell (its own position is fine
              // for the server's interaction distance)
        if (now - lastFurnaceMs_ > 600) {
            lastFurnaceMs_ = now;
            ++walkSteps_;
            sendMovePlayer(furnaceX_ + 0.5, furnaceY_ + 1.62, furnaceZ_ + 0.5, 0.0f, 0.0f,
                           true);
            if (walkSteps_ >= 3) {
                furnacePhase_ = 2;
                phaseStartMs_ = now;
                lastFurnaceMs_ = now;
            }
        }
        break;
    }
    case 2: { // tap the furnace until the window opens (max 3 tries)
        if (containerOpens_ >= 1) {
            if (now - lastFurnaceMs_ > 1500) {
                furnacePhase_ = 3;
                phaseStartMs_ = now;
            }
            break;
        }
        if (now - lastFurnaceMs_ > 1500) {
            lastFurnaceMs_ = now;
            if (useItemSent_ >= 3) {
                fail("server never opened the furnace window after 3 taps");
                furnacePhase_ = 6;
                break;
            }
            sendUseItem(furnaceX_, furnaceY_, furnaceZ_, 1 /* top face */,
                        furnaceX_ + 0.5, furnaceY_ + 1.62, furnaceZ_ + 0.5);
        }
        break;
    }
    case 3: { // close the window like a real player
        if (now - lastFurnaceMs_ > 1200) {
            lastFurnaceMs_ = now;
            sendContainerClose(containerWindowId_);
            furnacePhase_ = 4;
            phaseStartMs_ = now;
        }
        break;
    }
    case 4: { // re-open: second open path (existing tile reuse)
        if (containerOpens_ >= 2) {
            if (now - lastFurnaceMs_ > 1500) {
                furnacePhase_ = 5;
                phaseStartMs_ = now;
            }
            break;
        }
        if (now - lastFurnaceMs_ > 1500) {
            lastFurnaceMs_ = now;
            sendUseItem(furnaceX_, furnaceY_, furnaceZ_, 1,
                        furnaceX_ + 0.5, furnaceY_ + 1.62, furnaceZ_ + 0.5);
        }
        break;
    }
    case 5: { // final close
        if (now - lastFurnaceMs_ > 1200) {
            lastFurnaceMs_ = now;
            sendContainerClose(containerWindowId_);
            furnacePhase_ = 6;
            phaseStartMs_ = now;
            if (verbose_)
                fprintf(stderr, "[bot] furnace cycle complete: opens=%u closes=%u "
                                "blockEntityDatas=%u\n",
                        containerOpens_, closeSent_, blockEntityDatas_);
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// main loop
// ---------------------------------------------------------------------------
bool BotClient::goalReached(Scenario s) const {
    switch (s) {
    case Scenario::Login:
        return loginAccepted_;
    case Scenario::Chunks:
        return spawned_ && chunksReceived_ > 0;
    case Scenario::Move:
        return spawned_ && chatSent_ == 0 && malformedSent_ == 0;
    case Scenario::Chat:
        return spawned_ && chatSent_ > 0;
    case Scenario::Malformed:
        return malformedDone_;
    case Scenario::Furnace:
        return furnacePhase_ == 6 && containerOpens_ >= 2 && closeSent_ >= 2;
    }
    return false;
}

std::string BotClient::statsLine() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "state=%d login=%d spawn=%d chunks=%u chunkBytes=%llu packets=%zu "
                  "furnace=[%d,%d,%d] opens=%u closes=%u bed=%u",
                  (int)state_, (int)loginAccepted_, (int)spawned_, chunksReceived_,
                  (unsigned long long)chunkBytes_, packetCounts_.size(), furnaceX_, furnaceY_,
                  furnaceZ_, containerOpens_, closeSent_, blockEntityDatas_);
    return std::string(buf);
}

bool BotClient::run(uint64_t durationMs, Scenario scenario, const std::string& capturePath,
                    const std::string& username, bool verbose) {
    verbose_ = verbose;
    openCapture(capturePath);

    uint64_t start = nowMs();
    uint64_t lastHsRetryMs = start;
    int hsRetries = 0;
    state_ = State::Connecting1;
    sendOcr1();

    bool loggedIn = false;
    std::vector<uint8_t> dgram;
    int malformedKind = 0;
    uint64_t malformedNextMs = 0;
    uint64_t malformedDoneAt = 0;
    bool serverAliveCheckDone = false;
    bool serverAlive = false;

    while (nowMs() - start < durationMs) {
        while (recvOnce(dgram, 10)) {
            handleDatagram(dgram);
            dgram.clear();
        }
        flushAcks();
        retransmitStale();

        uint64_t now = nowMs();

        // handshake retries
        if ((state_ == State::Connecting1 || state_ == State::Connecting2) &&
            now - lastHsRetryMs > 2000) {
            lastHsRetryMs = now;
            if (++hsRetries > 5) {
                fail("raknet handshake timed out (state=" + std::to_string((int)state_) + ")");
                break;
            }
            if (state_ == State::Connecting1)
                sendOcr1();
            else
                sendClientConnect();
        }

        // login once the RakNet session is open
        if (state_ == State::Connected && !loggedIn) {
            loggedIn = true;
            doLogin(username);
            lastPingMs_ = now;
        }

        // chunk radius after a successful login (what the real client does)
        if (loginAccepted_ && !radiusSent_)
            sendRequestChunkRadius(radius_);

        // scenario traffic
        if (scenario == Scenario::Move && spawned_ && now - lastMoveMs_ > 1000) {
            lastMoveMs_ = now;
            moveX_ += 0.3;
            sendMovePlayer(moveX_, moveY_, moveZ_, 0.0f, 0.0f, true);
        }
        if (scenario == Scenario::Chat && spawned_ && now - lastChatMs_ > 3000) {
            lastChatMs_ = now;
            sendChat("hello from cursestone-bot #" + std::to_string(chatSent_));
        }
        if (scenario == Scenario::Furnace && spawned_)
            furnaceTick(now);
        if (now - lastPingMs_ > 5000) {
            lastPingMs_ = now;
            if (state_ == State::Connected)
                sendRakPing();
        }

        // malformed scenario: one bad packet per second, six kinds, then a
        // live check — the session must survive every single one. Starts
        // after the login was accepted, so the server has an active player
        // session when the corrupt traffic arrives.
        if (scenario == Scenario::Malformed && state_ == State::Connected && loginAccepted_ &&
            !malformedDone_) {
            if (now >= malformedNextMs) {
                sendMalformed(malformedKind++);
                if (malformedKind > 5) {
                    malformedDone_ = true;
                    malformedDoneAt = now;
                } else {
                    malformedNextMs = now + 1000;
                }
            }
        }
        if (scenario == Scenario::Malformed && malformedDone_ && !serverAliveCheckDone &&
            now - malformedDoneAt > 2000) {
            serverAliveCheckDone = true;
            // unconnected ping from the same socket: the server must answer
            // UNCONNECTED_PONG if it is still healthy.
            std::vector<uint8_t> ping;
            putU8(ping, 0x01);
            putU64LE(ping, now);
            putBytes(ping, RAK_MAGIC, 16);
            putU64BE(ping, guid_);
            sendRaw(ping);
            uint64_t waitStart = now;
            while (nowMs() - waitStart < 3000) {
                if (recvOnce(dgram, 100)) {
                    if (dgram[0] == 0x1c) {
                        serverAlive = true;
                        break;
                    }
                }
            }
        }

        // stop early once the goal is reached (except move/chat/furnace
        // which run on to gather trailing packets)
        if (goalReached(scenario) && scenario == Scenario::Login)
            break;
        if (goalReached(scenario) && scenario == Scenario::Furnace &&
            now - phaseStartMs_ > 1500)
            break;
        if (goalReached(scenario) && scenario == Scenario::Malformed &&
            serverAliveCheckDone) {
            if (serverAlive) {
                // keep the report positive only if the session survived
            } else {
                fail("server did not answer the unconnected ping after malformed traffic");
            }
            break;
        }
        if (state_ == State::Closed)
            break;
    }

    if (state_ >= State::Connected)
        sendDisconnect();
    closeCapture();

    if (verbose_)
        fprintf(stderr, "[bot] done: %s error=%s\n", statsLine().c_str(),
                lastError_.empty() ? "none" : lastError_.c_str());
    return lastError_.empty();
}

} // namespace csbot
