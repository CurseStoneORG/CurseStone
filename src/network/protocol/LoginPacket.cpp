// SPDX-License-Identifier: LGPL-3.0-or-later
// LoginPacket.cpp — decode the chain + clientData JWTs (protocol 84).
//
// Wire format (PMMP LoginPacket::decode):
//   [Int32 BE] protocol version (PMMP: getInt() reads 4 BE bytes).
//   [Uint8  ] edition (0 = Pocket)
//   [Int32 BE] outerPayload length (compressed)
//   [N bytes ] outerPayload (zlib stream)
//   decompressed payload layout:
//     [LInt32 LE] chainJson length  (PMMP: $this->getLInt())
//     [N bytes  ] chainJson (JSON object: {"chain": [jwt1, jwt2, ...]})
//     [LInt32 LE] clientDataJwt length
//     [N bytes  ] clientDataJwt (3-dot-separated JWT)
#include "LoginPacket.h"

#include <cstring>
#include <zlib.h>

namespace mcppeserver::network::protocol {

// ---- Minimal base64url decoder (no padding) ----
static bool base64UrlDecode(const std::string& in, std::vector<uint8_t>& out) {
    auto lookup = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    };
    uint32_t buf = 0;
    int bits = 0;
    out.clear();
    for (char c : in) {
        int v = lookup(c);
        if (v < 0) continue;
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xff));
        }
    }
    return true;
}

// Naive JSON value extractor — returns the string value at `key`.
// Handles: "key":"value" and "key":int. Sufficient for the well-known
// clientData fields (well-formed JWT payloads produced by MCPE clients).
static std::string extractJsonString(const std::string& json,
                                       const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}
static int extractJsonInt(const std::string& json,
                           const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return -1;
    pos += needle.size();
    int sign = 1;
    if (pos < json.size() && json[pos] == '-') { sign = -1; ++pos; }
    int v = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        v = v * 10 + (json[pos] - '0');
        ++pos;
    }
    return v * sign;
}

void LoginPacket::encode(std::vector<uint8_t>& /*out*/) const {
    // LoginPacket is serverbound; encode() is a no-op.
}

bool LoginPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);

    // PMMP reads protocol as getInt() (BE int32).
    int32_t proto32 = 0;
    if (!r.readI32BE(proto32)) return false;
    protocol = static_cast<int>(proto32);

    // PMMP semantics: if the protocol is not in ACCEPTED_PROTOCOLS, return
    // immediately WITHOUT attempting to decode any further fields. This
    // prevents reading garbage bytes (edition/outerLen/zlib payload) when
    // an outdated client sends a short buffer with the wrong protocol
    // number. handleLogin() will inspect `protocol` and reject with the
    // appropriate PlayStatusPacket (LOGIN_FAILED_CLIENT / _SERVER).
    if (protocol != CURRENT_PROTOCOL) return true;

    if (!r.readU8(edition)) return false;

    // outerPayload length (BE int32) and zlib-compressed payload.
    uint32_t outerLen = 0;
    if (!r.readU32BE(outerLen)) return false;
    if (outerLen > r.remaining()) return false;
    std::vector<uint8_t> compressed(outerLen);
    if (!r.readBytes(compressed.data(), outerLen)) return false;

    // Decompress with zlib (uncompress first, streaming inflate fallback).
    std::vector<uint8_t> inflated;
    {
        uLongf dstLen = 1 << 20;     // 1 MiB cap
        inflated.resize(dstLen);
        int rc = uncompress(inflated.data(), &dstLen,
                            compressed.data(), static_cast<uLong>(compressed.size()));
        if (rc == Z_OK) {
            inflated.resize(dstLen);
        } else {
            z_stream zs;
            std::memset(&zs, 0, sizeof(zs));
            if (inflateInit(&zs) != Z_OK) return false;
            zs.next_in  = const_cast<Bytef*>(compressed.data());
            zs.avail_in = static_cast<uInt>(compressed.size());
            inflated.clear();
            inflated.reserve(compressed.size() * 4);
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
                inflated.insert(inflated.end(), buf,
                                  buf + (sizeof(buf) - zs.avail_out));
            } while (ret != Z_STREAM_END);
            inflateEnd(&zs);
        }
    }

    // Now parse the decompressed payload:
    //   [LInt32 LE chainJsonLen][chainJson]
    //   [LInt32 LE cdJwtLen   ][cdJwt]
    PacketReader inner(inflated.data(), inflated.size());

    uint32_t chainLen = 0;
    if (!inner.readU32LE(chainLen)) return false;
    if (chainLen > inner.remaining()) return false;
    chainJson.assign(reinterpret_cast<const char*>(inner.ptr()), chainLen);
    if (!inner.skipBytes(chainLen)) return false;

    uint32_t cdLen = 0;
    if (!inner.readU32LE(cdLen)) return false;
    if (cdLen > inner.remaining()) return false;
    clientDataJwt.assign(reinterpret_cast<const char*>(inner.ptr()), cdLen);

    parseClientDataPayload();
    return true;
}

void LoginPacket::parseClientDataPayload() {
    // Split the JWT on '.' to get the payload (second segment).
    auto first = clientDataJwt.find('.');
    if (first == std::string::npos) return;
    auto second = clientDataJwt.find('.', first + 1);
    if (second == std::string::npos) return;
    std::string payloadB64 = clientDataJwt.substr(first + 1, second - first - 1);

    std::vector<uint8_t> decoded;
    if (!base64UrlDecode(payloadB64, decoded)) return;
    std::string payload(decoded.begin(), decoded.end());

    username      = extractJsonString(payload, "DisplayName");
    if (username.empty()) username = extractJsonString(payload, "displayName");
    clientUUID    = extractJsonString(payload, "Identity");
    if (clientUUID.empty()) clientUUID = extractJsonString(payload, "identity");
    skinId        = extractJsonString(payload, "SkinId");
    languageCode  = extractJsonString(payload, "LanguageCode");
    deviceModel   = extractJsonString(payload, "DeviceModel");
    deviceOS      = extractJsonInt  (payload, "DeviceOS");
    clientId      = extractJsonInt  (payload, "ClientRandomId");
    if (clientId == -1) clientId = extractJsonInt(payload, "ClientId");

    // Skin data is a base64 string; decoded into skinData.
    std::string skinB64 = extractJsonString(payload, "SkinData");
    base64UrlDecode(skinB64, skinData);
}

} // namespace mcppeserver::network::protocol
