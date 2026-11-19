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
// PacketSanitizer.cpp — per-packet outbound byte validation.
//
// Layouts (all verified against the matching encode() in this repo):
//   MoveEntity 0x0f     [eid I64BE][x y z F32BE][pitch][yaw][headYaw]      (23 B)
//   MovePlayer 0x10     [eid I64BE][x y z yaw bodyYaw pitch F32BE][mode][ground] (34 B)
//   SetEntityMotion 0x23 flat run of [eid I64BE][mx my mz F32BE] (stride 20)
//   AddEntity 0x0b      [eid I64BE][type I32BE][x y z][vx vy vz][yaw pitch] F32BE
//   AddPlayer 0x0a      [uuid 16][name BEString][eid I64BE][x y z][vx vy vz][yaw headYaw pitch] F32BE [slot][0x7f]
//   AddItemEntity 0x0d  [eid I64BE][slot][x y z][vx vy vz] F32BE
//   Explode 0x15        [x y z radius F32BE][count I32BE][records I32BE x3]
//   UpdateAttributes 0x1a [eid I64BE][count U16BE] then per entry
//                       [min max value F32BE][name BEString]
//   SetEntityData 0x22  [eid I64BE][metadata: key byte, per-type value, 0x7f]
//                       metadata values are LITTLE-endian (Binary::writeLShort etc.)
//   ContainerSetContent 0x2e [windowid U8][count U16BE][slots][hotbar block]
//                       slot: id U16BE; id==0 -> 2 bytes total, else
//                       id+count+damage (5 B) + nbtLen U16LE + nbt
#include "PacketSanitizer.h"

#include "SetEntityDataPacket.h" // TERMINATOR — the metadata end byte
#include "ContainerSetContentPacket.h" // SPECIAL_INVENTORY — hotbar block window

#include <bit>
#include <cstdio>
#include <cmath>

namespace cursestone::network::protocol {

namespace {

// ---- fixed-width readers (unaligned-safe, no float arithmetic) ----------

inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline uint32_t le32(const uint8_t* p) {
    return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]);
}
inline uint16_t be16(const uint8_t* p) {
    return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}
inline uint16_t le16(const uint8_t* p) {
    return uint16_t((uint16_t(p[1]) << 8) | p[0]);
}

// NaN and ±Inf share the all-ones exponent. One AND, no compare against
// float constants: keeps the hot path in integer registers.
inline bool badF32(uint32_t bits) {
    return (bits & 0x7f800000u) == 0x7f800000u;
}

inline bool isInfBits(uint32_t bits) {
    return (bits & 0x7fffffffu) == 0x7f800000u;
}

inline float bitsToF32(uint32_t bits) {
    // C++20 bit conversion: a compile-time-checked reinterpret that every
    // supported toolchain (MSVC, GCC, Clang/NDK) lowers to a single register
    // move — no float arithmetic, no compiler-specific __builtin_memcpy.
    static_assert(sizeof(float) == sizeof(bits), "float must be 32-bit");
    return std::bit_cast<float>(bits);
}

// Violation builders. `field`/`problem` are static literals — no ownership,
// no allocation; the log formatter copies them into the report string.
SanitizeViolation badFloat(const char* field, uint32_t bits, size_t offset) {
    SanitizeViolation v;
    v.violated = true;
    v.field = field;
    v.problem = isInfBits(bits) ? "infinite" : "NaN";
    v.bits = bits;
    v.value = bitsToF32(bits);
    v.offset = offset;
    return v;
}
SanitizeViolation problem(const char* field, const char* what, size_t offset) {
    SanitizeViolation v;
    v.violated = true;
    v.field = field;
    v.problem = what;
    v.offset = offset;
    return v;
}

// Absolute bound for one packet body: the largest legitimate payload this
// server produces is a serialized chunk (~100 KB). Anything near 4 MiB is
// a producer bug and the client would OOM trying to inflate it.
constexpr size_t kMaxPayloadBytes = 4u * 1024u * 1024u;

// Walk helpers shared by the two slot-bearing packets. Mirrors the encoders'
// putSlot: id==0 -> 2 bytes total, else 5 bytes + U16LE nbt length + nbt.
// Returns the offset just past the slot, or 0 when the bytes run out
// (0 is never a valid "past" offset because a slot is at least 2 bytes and
// the caller only invokes this at len > 0).
inline size_t skipSlot(const uint8_t* p, size_t len, size_t off) {
    if (off + 2 > len)
        return 0;
    uint16_t id = be16(p + off);
    if (id == 0)
        return off + 2;
    if (off + 5 > len)
        return 0;
    size_t nbt = le16(p + off + 5);
    size_t end = off + 7 + nbt;
    return end > len ? 0 : end;
}

// ---- per-packet validators ---------------------------------------------

SanitizeViolation checkMoveEntity(const uint8_t* p, size_t len) {
    if (len < 23)
        return problem("body", "truncated (needs 23 bytes)", len);
    if (badF32(be32(p + 8)))
        return badFloat("x", be32(p + 8), 8);
    if (badF32(be32(p + 12)))
        return badFloat("y", be32(p + 12), 12);
    if (badF32(be32(p + 16)))
        return badFloat("z", be32(p + 16), 16);
    return {};
}

SanitizeViolation checkMovePlayer(const uint8_t* p, size_t len) {
    if (len < 34)
        return problem("body", "truncated (needs 34 bytes)", len);
    for (int i = 0; i < 6; ++i) {
        size_t off = 8 + size_t(i) * 4;
        uint32_t bits = be32(p + off);
        if (badF32(bits)) {
            static const char* kFields[6] = {"x", "y", "z", "yaw", "bodyYaw", "pitch"};
            return badFloat(kFields[i], bits, off);
        }
    }
    return {};
}

SanitizeViolation checkSetEntityMotion(const uint8_t* p, size_t len) {
    if (len == 0)
        return {};
    if (len % 20 != 0)
        return problem("records", "truncated record run (stride 20)", len);
    for (size_t off = 0; off + 20 <= len; off += 20) {
        for (int i = 0; i < 3; ++i) {
            size_t fo = off + 8 + size_t(i) * 4;
            uint32_t bits = be32(p + fo);
            if (badF32(bits)) {
                static const char* kFields[3] = {"motionX", "motionY", "motionZ"};
                return badFloat(kFields[i], bits, fo);
            }
        }
    }
    return {};
}

SanitizeViolation checkAddEntity(const uint8_t* p, size_t len) {
    if (len < 44)
        return problem("body", "truncated (needs 44 bytes)", len);
    for (int i = 0; i < 8; ++i) {
        size_t off = 12 + size_t(i) * 4;
        uint32_t bits = be32(p + off);
        if (badF32(bits)) {
            static const char* kFields[8] = {"x", "y", "z", "speedX", "speedY", "speedZ",
                                             "yaw", "pitch"};
            return badFloat(kFields[i], bits, off);
        }
    }
    return {};
}

SanitizeViolation checkAddPlayer(const uint8_t* p, size_t len) {
    if (len < 18)
        return problem("body", "truncated before username length", len);
    size_t ulen = be16(p + 16);
    if (18 + ulen > len)
        return problem("username", "length prefix exceeds payload", 16);
    size_t base = 18 + ulen; // first byte after the username
    if (len < base + 44)
        return problem("body", "truncated spawn block", len);
    for (int i = 0; i < 9; ++i) {
        size_t off = base + 8 + size_t(i) * 4;
        uint32_t bits = be32(p + off);
        if (badF32(bits)) {
            static const char* kFields[9] = {"x", "y", "z", "speedX", "speedY", "speedZ",
                                             "yaw", "headYaw", "pitch"};
            return badFloat(kFields[i], bits, off);
        }
    }
    return {};
}

SanitizeViolation checkAddItemEntity(const uint8_t* p, size_t len) {
    if (len < 10)
        return problem("body", "truncated before slot", len);
    size_t after = skipSlot(p, len, 8);
    if (after == 0)
        return problem("slot", "slot walk out of bounds", 8);
    if (len < after + 24)
        return problem("body", "truncated position/velocity block", after);
    for (int i = 0; i < 6; ++i) {
        size_t off = after + size_t(i) * 4;
        uint32_t bits = be32(p + off);
        if (badF32(bits)) {
            static const char* kFields[6] = {"x", "y", "z", "speedX", "speedY", "speedZ"};
            return badFloat(kFields[i], bits, off);
        }
    }
    return {};
}

SanitizeViolation checkExplode(const uint8_t* p, size_t len) {
    if (len < 16)
        return problem("body", "truncated (needs 16 bytes)", len);
    for (int i = 0; i < 4; ++i) {
        size_t off = size_t(i) * 4;
        uint32_t bits = be32(p + off);
        if (badF32(bits)) {
            static const char* kFields[4] = {"x", "y", "z", "radius"};
            return badFloat(kFields[i], bits, off);
        }
    }
    if (len >= 20) {
        int64_t count = int32_t(be32(p + 16));
        if (count < 0 || 20 + count * 12 > int64_t(len))
            return problem("records", "record count exceeds payload", 16);
    }
    return {};
}

SanitizeViolation checkUpdateAttributes(const uint8_t* p, size_t len) {
    if (len < 10)
        return problem("body", "truncated (needs 10 bytes)", len);
    size_t count = be16(p + 8);
    if (count > 256)
        return problem("entry count", "absurd count (> 256)", 8);
    size_t off = 10;
    for (size_t i = 0; i < count; ++i) {
        if (off + 14 > len)
            return problem("entries", "entry walk out of bounds", off);
        for (int k = 0; k < 3; ++k) {
            size_t fo = off + size_t(k) * 4;
            uint32_t bits = be32(p + fo);
            if (badF32(bits)) {
                static const char* kFields[3] = {"attribute min", "attribute max",
                                                 "attribute value"};
                return badFloat(kFields[k], bits, fo);
            }
        }
        size_t nlen = be16(p + off + 12);
        off += 14 + nlen;
        if (off > len)
            return problem("attribute name", "length prefix exceeds payload", off - 14 - nlen);
    }
    return {};
}

SanitizeViolation checkSetEntityData(const uint8_t* p, size_t len) {
    if (len < 8)
        return problem("body", "truncated before metadata", len);
    // Metadata walk: byte-for-byte mirror of SetEntityDataPacket::encode
    // (little-endian values, type in the top 3 bits of the key byte,
    // 0x7f terminator). A walk that cannot reach the terminator means the
    // client's parser will desync on this packet.
    size_t off = 8;
    while (off < len) {
        uint8_t key = p[off];
        if (key == SetEntityDataPacket::TERMINATOR)
            return {};
        uint8_t type = uint8_t((key >> 5) & 0x07);
        size_t vsz;
        bool is_float = false;
        switch (type) {
        case 0: vsz = 1; break;            // byte
        case 1: vsz = 2; break;            // short (LE)
        case 2: vsz = 4; break;            // int (LE)
        case 3: vsz = 4; is_float = true; break; // float (LE)
        case 4: {                          // string: U16LE length + bytes
            if (off + 3 > len)
                return problem("metadata", "string length prefix out of bounds", off);
            vsz = 2 + le16(p + off + 1);
            break;
        }
        case 5: vsz = 0; break;            // slot: encoders write nothing here
        case 6: vsz = 12; break;           // pos: three LE int32
        default: vsz = 8; break;           // long (LE)
        }
        if (off + 1 + vsz > len)
            return problem("metadata", "walk out of bounds (parser desync)", off);
        if (is_float) {
            uint32_t bits = le32(p + off + 1);
            if (badF32(bits))
                return badFloat("metadata float", bits, off + 1);
        }
        off += 1 + vsz;
    }
    return problem("metadata", "missing 0x7f terminator", len);
}

SanitizeViolation checkContainerSetContent(const uint8_t* p, size_t len) {
    if (len < 3)
        return problem("body", "truncated (needs 3 bytes)", len);
    size_t count = be16(p + 1);
    // Pre-filter only: the legitimate maximum is the creative catalog
    // (windowid 0x79, ~570 slots measured live), so anything past 2048 is
    // garbage; smaller garbage counts are caught by the payload walk below.
    if (count > 2048)
        return problem("slot count", "absurd count (> 2048)", 1);
    if (3 + count * 2 > len)
        return problem("slot count", "count exceeds payload (parser desync)", 1);
    size_t off = 3;
    for (size_t i = 0; i < count; ++i) {
        if (off + 2 > len)
            return problem("slots", "slot walk out of bounds", off);
        uint16_t id = be16(p + off);
        if (id == 0) {
            off += 2;
            continue;
        }
        if (off + 5 > len)
            return problem("slots", "truncated slot header", off);
        size_t nbt = le16(p + off + 5);
        off += 7 + nbt;
        if (off > len)
            return problem("slot nbt", "length prefix exceeds payload", off - 7 - nbt);
    }
    // Trailing hotbar block: U16BE count, and when windowid ==
    // SPECIAL_INVENTORY (the player inventory) with a non-zero count, that
    // many I32BE link indices.
    if (off + 2 > len)
        return problem("hotbar count", "missing trailing count", off);
    size_t hotbar = be16(p + off);
    if (p[0] == ContainerSetContentPacket::SPECIAL_INVENTORY && hotbar > 0 &&
        off + 2 + hotbar * 4 > len)
        return problem("hotbar", "list exceeds payload (parser desync)", off);
    return {};
}

} // namespace

SanitizeViolation sanitizeOutbound(uint8_t pid, const uint8_t* payload, size_t len) {
    if (payload == nullptr)
        return problem("body", "null payload", 0);
    if (len > kMaxPayloadBytes)
        return problem("body", "payload exceeds 4 MiB", 0);
    switch (pid) {
    case MOVE_ENTITY_PACKET:
        return checkMoveEntity(payload, len);
    case MOVE_PLAYER_PACKET:
        return checkMovePlayer(payload, len);
    case SET_ENTITY_MOTION_PACKET:
        return checkSetEntityMotion(payload, len);
    case ADD_ENTITY_PACKET:
        return checkAddEntity(payload, len);
    case ADD_PLAYER_PACKET:
        return checkAddPlayer(payload, len);
    case ADD_ITEM_ENTITY_PACKET:
        return checkAddItemEntity(payload, len);
    case EXPLODE_PACKET:
        return checkExplode(payload, len);
    case UPDATE_ATTRIBUTES_PACKET:
        return checkUpdateAttributes(payload, len);
    case SET_ENTITY_DATA_PACKET:
        return checkSetEntityData(payload, len);
    case CONTAINER_SET_CONTENT_PACKET:
        return checkContainerSetContent(payload, len);
    default:
        return {};
    }
}

std::string hexPreview(const uint8_t* payload, size_t len, size_t max_bytes) {
    std::string out;
    if (payload == nullptr)
        return out;
    size_t cap = len < max_bytes ? len : max_bytes;
    out.reserve(cap * 3 + 4);
    char tmp[4];
    for (size_t i = 0; i < cap; ++i) {
        std::snprintf(tmp, sizeof(tmp), "%02x ", payload[i]);
        out += tmp;
    }
    if (len > cap)
        out += "...";
    return out;
}

std::string formatSuspectFloat(double v, uint32_t bits) {
    if (std::isnan(v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "NaN(0x%08x)", bits);
        return buf;
    }
    if (std::isinf(v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%cInf(0x%08x)", v < 0 ? '-' : '+', bits);
        return buf;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

} // namespace cursestone::network::protocol
