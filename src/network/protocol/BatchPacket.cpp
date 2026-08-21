// SPDX-License-Identifier: LGPL-3.0-or-later
// BatchPacket.cpp — raw deflate envelope (PMMP uses ZLIB_ENCODING_DEFLATE).
#include "BatchPacket.h"

#include <zlib.h>
#include <cstring>

namespace mcppeserver::network::protocol {

// Raw deflate: windowBits = -15 (negative => no zlib header / adler32).
static constexpr int RAW_DEFLATE_WINDOWBITS = -15;
static constexpr int DEFLATE_MEM_LEVEL      = 8;
static constexpr int DEFLATE_STRATEGY       = Z_DEFAULT_STRATEGY;

void BatchPacket::encode(std::vector<uint8_t>& out) const {
    // Outer length prefix is BE int32 (matches PMMP BatchPacket::encode -> putInt).
    // Body is raw-deflate-compressed payload_ (sequence of [BE int32 len][bytes]
    // sub-packets). PMMP uses zlib_encode($payload, ZLIB_ENCODING_DEFLATE) which
    // emits raw deflate (no zlib header / adler32). MCPE 0.15.10 expects exactly
    // that — emitting zlib format (header 0x78 0x9C + adler32) makes the client
    // reject every batch and the player never spawns.
    if (!compressed_.empty()) {
        uint32_t n = static_cast<uint32_t>(compressed_.size());
        out.push_back((uint8_t)(n >> 24));
        out.push_back((uint8_t)(n >> 16));
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)(n));
        out.insert(out.end(), compressed_.begin(), compressed_.end());
        return;
    }

    uLong srcLen = static_cast<uLong>(payload_.size());
    uLong bound  = compressBound(srcLen) + 64;   // small slack for raw deflate
    std::vector<uint8_t> tmp(bound);

    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    zs.next_in   = const_cast<Bytef*>(payload_.data());
    zs.avail_in  = static_cast<uInt>(srcLen);
    zs.next_out  = tmp.data();
    zs.avail_out = static_cast<uInt>(bound);

    // deflateInit2 with negative windowBits => raw deflate stream.
    if (deflateInit2(&zs,
                     /*level=*/7,
                     Z_DEFLATED,
                     RAW_DEFLATE_WINDOWBITS,
                     DEFLATE_MEM_LEVEL,
                     DEFLATE_STRATEGY) != Z_OK) {
        // Empty payload on failure (best-effort).
        for (int i = 0; i < 4; ++i) out.push_back(0);
        return;
    }

    int r = deflate(&zs, Z_FINISH);
    uLongf dstLen = static_cast<uLongf>(bound - zs.avail_out);
    deflateEnd(&zs);

    if (r != Z_STREAM_END && r != Z_OK) {
        // Compression failed — emit an empty payload so the wire shape
        // is still valid (caller will see an empty batch).
        for (int i = 0; i < 4; ++i) out.push_back(0);
        return;
    }

    uint32_t n = static_cast<uint32_t>(dstLen);
    out.push_back((uint8_t)(n >> 24));
    out.push_back((uint8_t)(n >> 16));
    out.push_back((uint8_t)(n >> 8));
    out.push_back((uint8_t)(n));
    out.insert(out.end(), tmp.data(), tmp.data() + dstLen);
}

bool BatchPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    // Outer length is BE int32 (PMMP: $size = $this->getInt(); $this->get($size))
    uint32_t compressedLen;
    if (!r.readU32BE(compressedLen)) return false;
    if (compressedLen > r.remaining()) return false;

    std::vector<uint8_t> compressed(compressedLen);
    if (!r.readBytes(compressed.data(), compressedLen)) return false;

    // Raw-inflate single-shot first (1 MiB cap, enough for MCPE batches).
    uLongf dstLen = 1 << 20;
    std::vector<uint8_t> dst(dstLen);

    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    // inflateInit2 with negative windowBits => raw inflate (no zlib header).
    if (inflateInit2(&zs, RAW_DEFLATE_WINDOWBITS) != Z_OK) return false;
    zs.next_in   = const_cast<Bytef*>(compressed.data());
    zs.avail_in  = static_cast<uInt>(compressedLen);
    zs.next_out  = dst.data();
    zs.avail_out = static_cast<uInt>(dstLen);
    int rc = inflate(&zs, Z_FINISH);
    uLongf produced = static_cast<uLongf>(dstLen - zs.avail_out);
    inflateEnd(&zs);

    if (rc == Z_STREAM_END || rc == Z_OK) {
        payload_.assign(dst.data(), dst.data() + produced);
        return true;
    }

    // Fallback: streaming raw inflate (handles larger batches that didn't
    // fit in the 1 MiB single-shot buffer).
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, RAW_DEFLATE_WINDOWBITS) != Z_OK) return false;
    zs.next_in   = const_cast<Bytef*>(compressed.data());
    zs.avail_in  = static_cast<uInt>(compressedLen);
    std::vector<uint8_t> inflated;
    inflated.reserve(compressedLen * 4);
    uint8_t buf[16 * 1024];
    int ret;
    do {
        zs.next_out  = buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT ||
            ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return false;
        }
        inflated.insert(inflated.end(), buf, buf + (sizeof(buf) - zs.avail_out));
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    payload_ = std::move(inflated);
    return true;
}

} // namespace mcppeserver::network::protocol
