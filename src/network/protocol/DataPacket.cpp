/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// DataPacket.cpp — PacketReader implementation.
#include "DataPacket.h"

#include <cstring>

namespace cursestone::network::protocol {

bool PacketReader::readU8(uint8_t& v) {
    if (off_ + 1 > len_)
        return false;
    v = data_[off_++];
    return true;
}
bool PacketReader::readI8(int8_t& v) {
    uint8_t u;
    if (!readU8(u))
        return false;
    v = (int8_t)u;
    return true;
}
bool PacketReader::readU16LE(uint16_t& v) {
    if (off_ + 2 > len_)
        return false;
    v = (uint16_t)((uint16_t)data_[off_] | ((uint16_t)data_[off_ + 1] << 8));
    off_ += 2;
    return true;
}
bool PacketReader::readU32LE(uint32_t& v) {
    if (off_ + 4 > len_)
        return false;
    v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (uint32_t)data_[off_ + i] << (i * 8);
    off_ += 4;
    return true;
}
bool PacketReader::readU64LE(uint64_t& v) {
    if (off_ + 8 > len_)
        return false;
    v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (uint64_t)data_[off_ + i] << (i * 8);
    off_ += 8;
    return true;
}
bool PacketReader::readF32LE(float& v) {
    uint32_t u;
    if (!readU32LE(u))
        return false;
    memcpy(&v, &u, 4);
    return true;
}
bool PacketReader::readF64LE(double& v) {
    uint64_t u;
    if (!readU64LE(u))
        return false;
    memcpy(&v, &u, 8);
    return true;
}
// ---- Big-endian readers ----
bool PacketReader::readU16BE(uint16_t& v) {
    if (off_ + 2 > len_)
        return false;
    v = (uint16_t)(((uint16_t)data_[off_] << 8) | (uint16_t)data_[off_ + 1]);
    off_ += 2;
    return true;
}
bool PacketReader::readI16BE(int16_t& v) {
    uint16_t u;
    if (!readU16BE(u))
        return false;
    v = (int16_t)u;
    return true;
}
bool PacketReader::readU32BE(uint32_t& v) {
    if (off_ + 4 > len_)
        return false;
    v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (uint32_t)data_[off_ + i] << ((3 - i) * 8);
    off_ += 4;
    return true;
}
bool PacketReader::readI32BE(int32_t& v) {
    uint32_t u;
    if (!readU32BE(u))
        return false;
    v = (int32_t)u;
    return true;
}
bool PacketReader::readU64BE(uint64_t& v) {
    if (off_ + 8 > len_)
        return false;
    v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (uint64_t)data_[off_ + i] << ((7 - i) * 8);
    off_ += 8;
    return true;
}
bool PacketReader::readI64BE(int64_t& v) {
    uint64_t u;
    if (!readU64BE(u))
        return false;
    v = (int64_t)u;
    return true;
}
bool PacketReader::readF32BE(float& v) {
    uint32_t u;
    if (!readU32BE(u))
        return false;
    memcpy(&v, &u, 4);
    return true;
}
bool PacketReader::readF64BE(double& v) {
    uint64_t u;
    if (!readU64BE(u))
        return false;
    memcpy(&v, &u, 8);
    return true;
}
bool PacketReader::readBytes(uint8_t* out, size_t n) {
    if (off_ + n > len_)
        return false;
    if (out)
        memcpy(out, data_ + off_, n);
    off_ += n;
    return true;
}
bool PacketReader::skipBytes(size_t n) {
    if (off_ + n > len_)
        return false;
    off_ += n;
    return true;
}
bool PacketReader::readString(std::string& out) {
    uint32_t len;
    if (!readVarU32(len))
        return false;
    if (off_ + len > len_)
        return false;
    out.assign(reinterpret_cast<const char*>(data_ + off_), len);
    off_ += len;
    return true;
}
bool PacketReader::readBEString(std::string& out) {
    uint16_t len;
    if (!readU16BE(len))
        return false;
    if (off_ + len > len_)
        return false;
    out.assign(reinterpret_cast<const char*>(data_ + off_), len);
    off_ += len;
    return true;
}
bool PacketReader::readVarU32(uint32_t& v) {
    uint32_t result = 0;
    size_t shift = 0;
    while (off_ < len_) {
        uint8_t b = data_[off_++];
        result |= (uint32_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            v = result;
            return true;
        }
        shift += 7;
        if (shift >= 35)
            return false;
    }
    return false;
}
bool PacketReader::readVarU64(uint64_t& v) {
    uint64_t result = 0;
    size_t shift = 0;
    while (off_ < len_) {
        uint8_t b = data_[off_++];
        result |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            v = result;
            return true;
        }
        shift += 7;
        if (shift >= 70)
            return false;
    }
    return false;
}
bool PacketReader::readVarI32(int32_t& v) {
    uint32_t u;
    if (!readVarU32(u))
        return false;
    v = mcp_varint_unzigzag_i32(u);
    return true;
}
bool PacketReader::readVarI64(int64_t& v) {
    uint64_t u;
    if (!readVarU64(u))
        return false;
    v = mcp_varint_unzigzag_i64(u);
    return true;
}

} // namespace cursestone::network::protocol
