// SPDX-License-Identifier: LGPL-3.0-or-later
// DataPacket — base class for all MCPE protocol-84 packets.
// Provides:
//   - getPid() / NETWORK_ID (subclass override)
//   - decode() / encode() — serializes/deserializes the packet body
//   - putPacketId() — writes the leading 1-byte ID + subclient prefix
//   - decode() helper that returns a Packet* subclass via the registry
//
// C++20. Uses std::vector<uint8_t> for buffers (memory-safe, no manual free).
#pragma once

#include "Info.h"
#include "../../utils/Binary.h"
#include <vector>
#include <cstdint>
#include <memory>
#include <span>
#include <string>           // required because writeBEString/writeString take const std::string&

namespace atomikpe::network::protocol {

class DataPacket {
public:
    virtual ~DataPacket() = default;
    virtual uint8_t pid() const = 0;

    // Encode this packet into `out` (without leading ID byte).
    virtual void encode(std::vector<uint8_t>& out) const = 0;
    // Decode from `in` (without leading ID byte). Returns true on success.
    virtual bool decode(const uint8_t* data, size_t len) = 0;

    // True if this packet type is allowed before login (LoginPacket itself,
    // Ping, etc.). Default is false; subclasses override.
    virtual bool canBeSentBeforeLogin() const { return false; }

    // Whether the packet needs to be sent over a reliable ordered channel.
    // Default true; subclasses override for unreliable or sequenced packets.
    virtual bool isReliableOrdered() const { return true; }

    // Serialize the full packet (leading ID byte + body) into `out`.
    void writePacket(std::vector<uint8_t>& out) const {
        out.push_back(pid());
        encode(out);
    }
};

// Cursor-based decoder. Wraps a byte span and reads little-endian values.
class PacketReader {
public:
    PacketReader(const uint8_t* data, size_t len)
        : data_(data), len_(len), off_(0) {}

    bool eof() const { return off_ >= len_; }
    size_t remaining() const { return off_ < len_ ? len_ - off_ : 0; }
    const uint8_t* ptr() const { return data_ + off_; }

    bool readU8(uint8_t& v);
    bool readI8(int8_t& v);
    bool readU16LE(uint16_t& v);
    bool readU32LE(uint32_t& v);
    bool readU64LE(uint64_t& v);
    bool readF32LE(float& v);
    bool readF64LE(double& v);
    // Big-endian readers (PMMP uses putInt/putLong which are BE).
    bool readU16BE(uint16_t& v);
    bool readI16BE(int16_t& v);
    bool readU32BE(uint32_t& v);
    bool readI32BE(int32_t& v);
    bool readU64BE(uint64_t& v);
    bool readI64BE(int64_t& v);
    bool readF32BE(float& v);
    bool readF64BE(double& v);
    bool readBytes(uint8_t* out, size_t n);
    // Advances the cursor by `n` without copying.
    bool skipBytes(size_t n);
    bool readString(std::string& out);          // VarUInt length + bytes
    bool readBEString(std::string& out);        // 2-byte BE length + bytes
    bool readVarU32(uint32_t& v);
    bool readVarU64(uint64_t& v);
    bool readVarI32(int32_t& v);
    bool readVarI64(int64_t& v);

private:
    const uint8_t* data_;
    size_t len_;
    size_t off_;
};

// Cursor-based encoder. Appends little-endian values to a std::vector.
class PacketWriter {
public:
    explicit PacketWriter(std::vector<uint8_t>& out) : out_(out) {}

    // ---- Single byte ----
    void writeU8(uint8_t v) { out_.push_back(v); }
    void writeI8(int8_t v)  { out_.push_back((uint8_t)v); }

    // ---- Little-endian primitives ----
    void writeU16LE(uint16_t v) {
        out_.push_back((uint8_t)(v));
        out_.push_back((uint8_t)(v >> 8));
    }
    void writeU32LE(uint32_t v) {
        for (int i = 0; i < 4; ++i) out_.push_back((uint8_t)(v >> (i * 8)));
    }
    void writeU64LE(uint64_t v) {
        for (int i = 0; i < 8; ++i) out_.push_back((uint8_t)(v >> (i * 8)));
    }
    void writeF32LE(float v)  { uint32_t u; memcpy(&u, &v, 4); writeU32LE(u); }
    void writeF64LE(double v) { uint64_t u; memcpy(&u, &v, 8); writeU64LE(u); }

    // ---- Big-endian primitives ----
    // PMMP uses putInt/putLong/putShort/putFloat which are big-endian
    // (see pocketmine/utils/Binary.php::writeInt -> pack("N", ...) = BE32).
    // MCPE protocol 84 wire format expects BE for almost all numeric fields.
    void writeU16BE(uint16_t v) {
        out_.push_back((uint8_t)(v >> 8));
        out_.push_back((uint8_t)(v));
    }
    void writeI16BE(int16_t v) { writeU16BE((uint16_t)v); }
    void writeU32BE(uint32_t v) {
        out_.push_back((uint8_t)(v >> 24));
        out_.push_back((uint8_t)(v >> 16));
        out_.push_back((uint8_t)(v >> 8));
        out_.push_back((uint8_t)(v));
    }
    void writeI32BE(int32_t v) { writeU32BE((uint32_t)v); }
    void writeU64BE(uint64_t v) {
        for (int i = 7; i >= 0; --i) out_.push_back((uint8_t)(v >> (i * 8)));
    }
    void writeI64BE(int64_t v) { writeU64BE((uint64_t)v); }
    void writeF32BE(float v)   { uint32_t u; memcpy(&u, &v, 4); writeU32BE(u); }
    void writeF64BE(double v)  { uint64_t u; memcpy(&u, &v, 8); writeU64BE(u); }

    // BE-prefixed string: 2-byte BE length + bytes. Mirrors PMMP putString.
    void writeBEString(const std::string& s) {
        writeU16BE(static_cast<uint16_t>(s.size()));
        out_.insert(out_.end(), s.data(), s.data() + s.size());
    }
    // VarInt-prefixed string (used by some newer fields).
    void writeString(const std::string& s) {
        writeVarU32(static_cast<uint32_t>(s.size()));
        out_.insert(out_.end(), s.data(), s.data() + s.size());
    }
    void writeBytes(const uint8_t* p, size_t n) {
        out_.insert(out_.end(), p, p + n);
    }
    void writeVarU32(uint32_t v) {
        uint8_t buf[5];
        size_t n = mcp_varint_write_u32(buf, v);
        out_.insert(out_.end(), buf, buf + n);
    }
    void writeVarU64(uint64_t v) {
        uint8_t buf[10];
        size_t n = mcp_varint_write_u64(buf, v);
        out_.insert(out_.end(), buf, buf + n);
    }
    void writeVarI32(int32_t v) { writeVarU32(mcp_varint_zigzag_i32(v)); }
    void writeVarI64(int64_t v) { writeVarU64(mcp_varint_zigzag_i64(v)); }

private:
    std::vector<uint8_t>& out_;
};

} // namespace atomikpe::network::protocol
