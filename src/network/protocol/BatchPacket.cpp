/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
// BatchPacket.cpp — dual-format zlib/raw-deflate envelope.
// The reference server uses zlib_encode/zlib_decode (zlib format with 0x78 header).
// Some MCPE 0.15.10 clients use raw deflate (no header).
// We try both formats to handle all clients.
#include "BatchPacket.h"
#include <zlib.h>
#include <cstring>

namespace cursestone::network::protocol {

namespace {
// Low zlib level: encode() runs on the tick thread for every player each tick.
constexpr int kBatchCompressionLevel = 2;

// Working buffer sizing for the single-shot inflate attempts. The inflated
// batch is rarely much larger than the compressed stream (MCPE batches carry
// already-compressed chunk columns, so the ratio hovers near 1), so sizing
// from the compressed length avoids the old fixed 1 MiB zero-filled temporary
// per batch. The cap matches the hard ceiling the streaming fallback below
// enforces.
constexpr size_t kInflateMinBuffer = 64u << 10; // 64 KiB
constexpr size_t kInflateMaxBuffer = 8u << 20;  // 8 MiB (anti zlib-bomb ceiling)

size_t inflateWorkingSize(size_t compressedLen) {
    size_t want = compressedLen * 4;
    if (want < kInflateMinBuffer)
        want = kInflateMinBuffer;
    if (want > kInflateMaxBuffer)
        want = kInflateMaxBuffer;
    return want;
}

// One-shot inflate of `src` into `out` (sized to dstCap on entry). Returns
// true only when the whole stream decoded; on failure `out` is cleared and
// its capacity kept for the next attempt. Inflating straight from the
// inbound buffer and handing the result to the packet removes the two buffer
// copies the old path paid (compressed copy + payload assign).
bool inflateOnce(const uint8_t* src, size_t srcLen, size_t dstCap, bool raw,
                 std::vector<uint8_t>& out) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, raw ? -15 : 15) != Z_OK) // -15: raw deflate (no zlib header)
        return false;
    out.resize(dstCap);
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(srcLen);
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(dstCap);
    int rc = inflate(&zs, Z_FINISH);
    const size_t produced = dstCap - zs.avail_out;
    const bool complete = (rc == Z_STREAM_END);
    inflateEnd(&zs);
    if (!complete) {
        // A stream that outgrows the working buffer (Z_OK / Z_BUF_ERROR) or
        // corrupt input. The streaming fallback below decides oversized
        // batches; partial output must never be accepted as a payload.
        out.clear();
        return false;
    }
    out.resize(produced);
    return true;
}
} // namespace

void BatchPacket::encode(std::vector<uint8_t>& out) const {
    if (!compressed_.empty()) {
        uint32_t n = static_cast<uint32_t>(compressed_.size());
        out.push_back((uint8_t)(n >> 24));
        out.push_back((uint8_t)(n >> 16));
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)(n));
        out.insert(out.end(), compressed_.begin(), compressed_.end());
        return;
    }
    const uLong srcLen = static_cast<uLong>(payload_.size());
    const uLong bound = compressBound(srcLen) + 64;
    // Compress straight into the caller's buffer. The old path compressed
    // into a temporary vector and then copied the compressed bytes again on
    // insert — an extra full pass over every batch on the send path.
    const size_t base = out.size();
    out.resize(base + 4 + bound);
    uLongf dstLen = bound;
    int r = compress2(out.data() + base + 4, &dstLen, payload_.data(), srcLen,
                      kBatchCompressionLevel);
    if (r != Z_OK) {
        for (int i = 0; i < 4; ++i)
            out[base + i] = 0;
        out.resize(base + 4);
        return;
    }
    out.resize(base + 4 + dstLen);
    const uint32_t n = static_cast<uint32_t>(dstLen);
    out.data()[base] = (uint8_t)(n >> 24);
    out.data()[base + 1] = (uint8_t)(n >> 16);
    out.data()[base + 2] = (uint8_t)(n >> 8);
    out.data()[base + 3] = (uint8_t)n;
}

bool BatchPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    uint32_t compressedLen;
    if (!r.readU32BE(compressedLen))
        return false;
    if (compressedLen > r.remaining())
        return false;
    // Zero-copy view into the inbound buffer (the old code first copied the
    // whole compressed stream into a temporary vector).
    const uint8_t* src = r.ptr();
    const size_t work = inflateWorkingSize(compressedLen);

    // Try 1: zlib envelope — used by the reference server and the Python bot.
    if (inflateOnce(src, compressedLen, work, /*raw=*/false, payload_))
        return true;

    // Try 2: raw deflate — used by some MCPE clients (no zlib header).
    if (inflateOnce(src, compressedLen, work, /*raw=*/true, payload_))
        return true;

    // Try 3: streaming zlib inflate for batches larger than the working
    // buffer. Hard ceiling on inflated output: a zlib bomb can otherwise
    // expand ~1000:1 and the request is answered before any session exists.
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK)
        return false;
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(compressedLen);
    std::vector<uint8_t> inflated;
    inflated.reserve(compressedLen * 4);
    uint8_t buf[16 * 1024];
    int ret;
    do {
        zs.next_out = buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || ret == Z_DATA_ERROR ||
            ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return false;
        }
        size_t produced = sizeof(buf) - zs.avail_out;
        if (produced > 0) {
            if (inflated.size() + produced > kInflateMaxBuffer) {
                inflateEnd(&zs);
                return false;
            }
            inflated.insert(inflated.end(), buf, buf + produced);
        }
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    payload_ = std::move(inflated);
    return true;
}

} // namespace cursestone::network::protocol
