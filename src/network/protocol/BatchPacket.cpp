// SPDX-License-Identifier: LGPL-3.0-or-later
// BatchPacket.cpp — zlib format envelope (PMMP uses zlib_encode/zlib_decode).
#include "BatchPacket.h"
#include <zlib.h>
#include <cstring>

namespace mcppeserver::network::protocol {

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
    uLong srcLen = static_cast<uLong>(payload_.size());
    uLong bound  = compressBound(srcLen) + 64;
    std::vector<uint8_t> tmp(bound);
    uLongf dstLen = bound;
    int r = compress2(tmp.data(), &dstLen, payload_.data(), srcLen, 7);
    if (r != Z_OK) {
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
    uint32_t compressedLen;
    if (!r.readU32BE(compressedLen)) return false;
    if (compressedLen > r.remaining()) return false;
    std::vector<uint8_t> compressed(compressedLen);
    if (!r.readBytes(compressed.data(), compressedLen)) return false;
    uLongf dstLen = 1 << 20;
    std::vector<uint8_t> dst(dstLen);
    int rc = uncompress(dst.data(), &dstLen, compressed.data(), (uLong)compressedLen);
    if (rc == Z_OK) {
        payload_.assign(dst.data(), dst.data() + dstLen);
        return true;
    }
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) return false;
    zs.next_in  = const_cast<Bytef*>(compressed.data());
    zs.avail_in = (uInt)compressedLen;
    std::vector<uint8_t> inflated;
    inflated.reserve(compressedLen * 4);
    uint8_t buf[16 * 1024];
    int ret;
    do {
        zs.next_out  = buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
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
