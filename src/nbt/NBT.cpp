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
#include "tag/TagDefs.h"

#include <cstdlib>
#include <cstring>

// Maximum nesting depth accepted by the parser. Vanilla NBT stays far below
// this; the cap turns a wire-controlled recursion (stack exhaustion) into a
// normal parse failure.
static constexpr int MCP_NBT_MAX_DEPTH = 128;

// Read a uint16 length + bytes for a name field.
static bool read_name(mcp_bstream_t* s, bool le, std::string& out) {
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

static mcp_tag_t* read_payload_depth(mcp_bstream_t* s, mcp_tag_type_t type, bool le, int depth) {
    if (depth > MCP_NBT_MAX_DEPTH)
        return NULL;
    switch (type) {
    case MCP_TAG_BYTE: {
        int8_t v;
        if (mcp_bstream_read_i8(s, &v) != 0)
            return NULL;
        return mcp_byte_tag_new(v);
    }
    case MCP_TAG_SHORT: {
        uint16_t v;
        if (le ? mcp_bstream_read_u16_le(s, &v) : mcp_bstream_read_u16_be(s, &v))
            return NULL;
        return mcp_short_tag_new((int16_t)v);
    }
    case MCP_TAG_INT: {
        uint32_t v;
        if (le ? mcp_bstream_read_u32_le(s, &v) : mcp_bstream_read_u32_be(s, &v))
            return NULL;
        return mcp_int_tag_new((int32_t)v);
    }
    case MCP_TAG_LONG: {
        uint64_t v;
        if (le ? mcp_bstream_read_u64_le(s, &v) : mcp_bstream_read_u64_be(s, &v))
            return NULL;
        return mcp_long_tag_new((int64_t)v);
    }
    case MCP_TAG_FLOAT: {
        float v;
        if (le ? mcp_bstream_read_f32_le(s, &v) : mcp_bstream_read_f32_be(s, &v))
            return NULL;
        return mcp_float_tag_new(v);
    }
    case MCP_TAG_DOUBLE: {
        double v;
        if (le ? mcp_bstream_read_f64_le(s, &v) : mcp_bstream_read_f64_be(s, &v))
            return NULL;
        return mcp_double_tag_new(v);
    }
    case MCP_TAG_BYTE_ARRAY: {
        uint32_t len;
        if (le ? mcp_bstream_read_u32_le(s, &len) : mcp_bstream_read_u32_be(s, &len))
            return NULL;
        if (mcp_bstream_remaining(s) < len)
            return NULL;
        auto* t = new mcp_byte_array_tag();
        t->data.resize(len);
        if (len && mcp_bstream_read_bytes(s, t->data.data(), len) != 0) {
            delete t;
            return NULL;
        }
        return t;
    }
    case MCP_TAG_STRING: {
        uint16_t len;
        if (le ? mcp_bstream_read_u16_le(s, &len) : mcp_bstream_read_u16_be(s, &len))
            return NULL;
        if (mcp_bstream_remaining(s) < len)
            return NULL;
        auto* t = new mcp_string_tag();
        t->value.resize(len);
        if (len && mcp_bstream_read_bytes(s, &t->value[0], len) != 0) {
            delete t;
            return NULL;
        }
        return t;
    }
    case MCP_TAG_LIST: {
        uint8_t et;
        if (mcp_bstream_read_u8(s, &et) != 0)
            return NULL;
        uint32_t count;
        if (le ? mcp_bstream_read_u32_le(s, &count) : mcp_bstream_read_u32_be(s, &count))
            return NULL;
        mcp_tag_t* list = mcp_list_tag_new((mcp_tag_type_t)et);
        if (!list)
            return NULL;
        if (et == MCP_TAG_END || count == 0) {
            // Empty list; nothing more to read.
            return list;
        }
        for (uint32_t i = 0; i < count; ++i) {
            mcp_tag_t* child = read_payload_depth(s, (mcp_tag_type_t)et, le, depth + 1);
            if (!child || mcp_list_tag_append(list, child) != 0) {
                delete child; // list keeps it when append succeeded
                delete list;
                return NULL;
            }
        }
        return list;
    }
    case MCP_TAG_COMPOUND: {
        mcp_tag_t* comp = mcp_compound_tag_new();
        if (!comp)
            return NULL;
        while (true) {
            uint8_t childType;
            if (mcp_bstream_read_u8(s, &childType) != 0) {
                delete comp;
                return NULL;
            }
            if (childType == MCP_TAG_END)
                break;
            std::string name;
            if (!read_name(s, le, name)) {
                delete comp;
                return NULL;
            }
            mcp_tag_t* child = read_payload_depth(s, (mcp_tag_type_t)childType, le, depth + 1);
            if (!child) {
                delete comp;
                return NULL;
            }
            if (!name.empty())
                mcp_tag_set_name(child, name.data(), name.size());
            if (mcp_compound_tag_set(comp, name.c_str(), child) != 0) {
                delete child;
                delete comp;
                return NULL;
            }
        }
        return comp;
    }
    case MCP_TAG_INT_ARRAY: {
        uint32_t count;
        if (le ? mcp_bstream_read_u32_le(s, &count) : mcp_bstream_read_u32_be(s, &count))
            return NULL;
        // The wire must actually carry `count` ints. Division (not count*4)
        // so the check itself cannot overflow on 32-bit size_t.
        if (count > mcp_bstream_remaining(s) / sizeof(int32_t))
            return NULL;
        auto* t = new mcp_int_array_tag();
        t->data.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t v;
            if (le ? mcp_bstream_read_u32_le(s, &v) : mcp_bstream_read_u32_be(s, &v)) {
                delete t;
                return NULL;
            }
            t->data[i] = (int32_t)v;
        }
        return t;
    }
    case MCP_TAG_END:
    default:
        return NULL;
    }
}

static mcp_tag_t* read_root(mcp_bstream_t* s, bool le) {
    uint8_t type;
    if (mcp_bstream_read_u8(s, &type) != 0)
        return NULL;
    if (type == MCP_TAG_END)
        return mcp_end_tag_new();
    std::string name;
    if (!read_name(s, le, name))
        return NULL;
    mcp_tag_t* t = read_payload_depth(s, (mcp_tag_type_t)type, le, 0);
    if (!t)
        return NULL;
    if (!name.empty())
        mcp_tag_set_name(t, name.data(), name.size());
    return t;
}

static void write_root(mcp_bstream_t* s, const mcp_tag_t* root, bool le) {
    if (!root) {
        mcp_bstream_write_u8(s, (uint8_t)MCP_TAG_END);
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

mcp_tag_t* mcp_nbt_read_bytes(const uint8_t* data, size_t len, bool le) {
    try {
        mcp_bstream_t s;
        mcp_bstream_init_read(&s, data, len);
        mcp_tag_t* t = read_root(&s, le);
        mcp_bstream_free(&s);
        return t;
    } catch (...) {
        return NULL;
    }
}

uint8_t* mcp_nbt_write_bytes(const mcp_tag_t* root, bool le, size_t* out_len) {
    try {
        mcp_bstream_t s;
        mcp_bstream_init(&s);
        write_root(&s, root, le);
        uint8_t* out = (uint8_t*)malloc(s.size ? s.size : 1);
        if (!out) {
            mcp_bstream_free(&s);
            return NULL;
        }
        if (s.size)
            memcpy(out, s.data, s.size);
        if (out_len)
            *out_len = s.size;
        mcp_bstream_free(&s);
        return out;
    } catch (...) {
        return NULL;
    }
}
