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
// NBT.cpp — reader/writer implementation.
#include "NBT.h"
#include "tag/ByteArrayTag.h"
#include "tag/ByteTag.h"
#include "tag/CompoundTag.h"
#include "tag/DoubleTag.h"
#include "tag/EndTag.h"
#include "tag/FloatTag.h"
#include "tag/IntArrayTag.h"
#include "tag/IntTag.h"
#include "tag/ListTag.h"
#include "tag/LongTag.h"
#include "tag/ShortTag.h"
#include "tag/StringTag.h"

namespace cursestone::nbt {
namespace {

// Maximum nesting depth accepted by the parser. Vanilla NBT stays far below
// this; the cap turns a wire-controlled recursion (stack exhaustion) into a
// normal parse failure.
constexpr int MAX_DEPTH = 128;

// Read a uint16 length + bytes for a name field.
bool read_name(mcp_bstream_t* s, bool le, std::string& out) {
    uint16_t len;
    if (le ? mcp_bstream_read_u16_le(s, &len) : mcp_bstream_read_u16_be(s, &len))
        return false;
    if (mcp_bstream_remaining(s) < len)
        return false;
    out.resize(len);
    if (len && mcp_bstream_read_bytes(s, &out[0], len) != 0)
        return false;
    return true;
}

Tag* read_payload_depth(mcp_bstream_t* s, TagType type, bool le, int depth) {
    if (depth > MAX_DEPTH)
        return nullptr;
    switch (type) {
    case TAG_BYTE: {
        int8_t v;
        if (mcp_bstream_read_i8(s, &v) != 0)
            return nullptr;
        return new ByteTag(v);
    }
    case TAG_SHORT: {
        uint16_t v;
        if (le ? mcp_bstream_read_u16_le(s, &v) : mcp_bstream_read_u16_be(s, &v))
            return nullptr;
        return new ShortTag((int16_t)v);
    }
    case TAG_INT: {
        uint32_t v;
        if (le ? mcp_bstream_read_u32_le(s, &v) : mcp_bstream_read_u32_be(s, &v))
            return nullptr;
        return new IntTag((int32_t)v);
    }
    case TAG_LONG: {
        uint64_t v;
        if (le ? mcp_bstream_read_u64_le(s, &v) : mcp_bstream_read_u64_be(s, &v))
            return nullptr;
        return new LongTag((int64_t)v);
    }
    case TAG_FLOAT: {
        float v;
        if (le ? mcp_bstream_read_f32_le(s, &v) : mcp_bstream_read_f32_be(s, &v))
            return nullptr;
        return new FloatTag(v);
    }
    case TAG_DOUBLE: {
        double v;
        if (le ? mcp_bstream_read_f64_le(s, &v) : mcp_bstream_read_f64_be(s, &v))
            return nullptr;
        return new DoubleTag(v);
    }
    case TAG_BYTE_ARRAY: {
        uint32_t len;
        if (le ? mcp_bstream_read_u32_le(s, &len) : mcp_bstream_read_u32_be(s, &len))
            return nullptr;
        if (mcp_bstream_remaining(s) < len)
            return nullptr;
        auto* t = new ByteArrayTag();
        t->data.resize(len);
        if (len && mcp_bstream_read_bytes(s, t->data.data(), len) != 0) {
            delete t;
            return nullptr;
        }
        return t;
    }
    case TAG_STRING: {
        uint16_t len;
        if (le ? mcp_bstream_read_u16_le(s, &len) : mcp_bstream_read_u16_be(s, &len))
            return nullptr;
        if (mcp_bstream_remaining(s) < len)
            return nullptr;
        auto* t = new StringTag("");
        t->value.resize(len);
        if (len && mcp_bstream_read_bytes(s, &t->value[0], len) != 0) {
            delete t;
            return nullptr;
        }
        return t;
    }
    case TAG_LIST: {
        uint8_t et;
        if (mcp_bstream_read_u8(s, &et) != 0)
            return nullptr;
        uint32_t count;
        if (le ? mcp_bstream_read_u32_le(s, &count) : mcp_bstream_read_u32_be(s, &count))
            return nullptr;
        auto* list = new ListTag((TagType)et);
        if (et == TAG_END || count == 0) {
            // Empty list; nothing more to read.
            return list;
        }
        for (uint32_t i = 0; i < count; ++i) {
            Tag* child = read_payload_depth(s, (TagType)et, le, depth + 1);
            if (!child || !append(list, child)) {
                delete child; // the list keeps it when append succeeded
                delete list;
                return nullptr;
            }
        }
        return list;
    }
    case TAG_COMPOUND: {
        auto* comp = new CompoundTag();
        while (true) {
            uint8_t childType;
            if (mcp_bstream_read_u8(s, &childType) != 0) {
                delete comp;
                return nullptr;
            }
            if (childType == TAG_END)
                break;
            std::string name;
            if (!read_name(s, le, name)) {
                delete comp;
                return nullptr;
            }
            Tag* child = read_payload_depth(s, (TagType)childType, le, depth + 1);
            if (!child) {
                delete comp;
                return nullptr;
            }
            if (!name.empty())
                set_name(child, name.data(), name.size());
            if (!set(comp, name.c_str(), child)) {
                delete child;
                delete comp;
                return nullptr;
            }
        }
        return comp;
    }
    case TAG_INT_ARRAY: {
        uint32_t count;
        if (le ? mcp_bstream_read_u32_le(s, &count) : mcp_bstream_read_u32_be(s, &count))
            return nullptr;
        // The wire must actually carry `count` ints. Division (not count*4)
        // so the check itself cannot overflow on 32-bit size_t.
        if (count > mcp_bstream_remaining(s) / sizeof(int32_t))
            return nullptr;
        auto* t = new IntArrayTag();
        t->data.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t v;
            if (le ? mcp_bstream_read_u32_le(s, &v) : mcp_bstream_read_u32_be(s, &v)) {
                delete t;
                return nullptr;
            }
            t->data[i] = (int32_t)v;
        }
        return t;
    }
    case TAG_END:
    default:
        return nullptr;
    }
}

Tag* read_root(mcp_bstream_t* s, bool le) {
    uint8_t type;
    if (mcp_bstream_read_u8(s, &type) != 0)
        return nullptr;
    if (type == TAG_END)
        return new EndTag();
    std::string name;
    if (!read_name(s, le, name))
        return nullptr;
    Tag* t = read_payload_depth(s, (TagType)type, le, 0);
    if (!t)
        return nullptr;
    if (!name.empty())
        set_name(t, name.data(), name.size());
    return t;
}

void write_root(mcp_bstream_t* s, const Tag* root, bool le) {
    if (!root) {
        mcp_bstream_write_u8(s, (uint8_t)TAG_END);
        return;
    }
    mcp_bstream_write_u8(s, (uint8_t)root->type);
    if (le)
        mcp_bstream_write_u16_le(s, (uint16_t)root->name.size());
    else
        mcp_bstream_write_u16_be(s, (uint16_t)root->name.size());
    if (!root->name.empty())
        mcp_bstream_write_bytes(s, root->name.data(), root->name.size());
    root->write_payload(s, le);
}

} // namespace

Tag* read(const uint8_t* data, size_t len, bool le) {
    try {
        mcp_bstream_t s;
        mcp_bstream_init_read(&s, data, len);
        Tag* t = read_root(&s, le);
        mcp_bstream_free(&s);
        return t;
    } catch (...) {
        return nullptr;
    }
}

void write(const Tag* root, bool le, std::vector<uint8_t>& out) {
    out.clear();
    try {
        mcp_bstream_t s;
        mcp_bstream_init(&s);
        write_root(&s, root, le);
        if (s.size)
            out.assign(s.data, s.data + s.size);
        mcp_bstream_free(&s);
    } catch (...) {
        out.clear();
    }
}

} // namespace cursestone::nbt
