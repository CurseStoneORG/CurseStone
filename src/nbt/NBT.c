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
// NBT.c — reader/writer implementation. Pure C.
#include "NBT.h"
#include "tag/ByteTag.h"
#include "tag/ShortTag.h"
#include "tag/IntTag.h"
#include "tag/LongTag.h"
#include "tag/FloatTag.h"
#include "tag/DoubleTag.h"
#include "tag/ByteArrayTag.h"
#include "tag/StringTag.h"
#include "tag/ListTag.h"
#include "tag/CompoundTag.h"
#include "tag/IntArrayTag.h"
#include "tag/EndTag.h"

#include <stdlib.h>
#include <string.h>

// Read a uint16 length + bytes for a name field. Caller frees the result.
static char* read_name(mcp_bstream_t* s, bool le, size_t* out_len) {
    uint16_t len;
    if (le) {
        if (mcp_bstream_read_u16_le(s, &len) != 0) return NULL;
    } else {
        if (mcp_bstream_read_u16_be(s, &len) != 0) return NULL;
    }
    if (mcp_bstream_remaining(s) < len) return NULL;
    char* buf = (char*)malloc(len + 1);
    if (!buf) return NULL;
    if (len) {
        if (mcp_bstream_read_bytes(s, buf, len) != 0) {
            free(buf);
            return NULL;
        }
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

mcp_tag_t* mcp_nbt_read_payload(mcp_bstream_t* s, mcp_tag_type_t type,
                                  bool le) {
    switch (type) {
        case MCP_TAG_BYTE: {
            int8_t v;
            if (mcp_bstream_read_i8(s, &v) != 0) return NULL;
            return mcp_byte_tag_new(v);
        }
        case MCP_TAG_SHORT: {
            uint16_t v;
            if (le ? mcp_bstream_read_u16_le(s, &v) : mcp_bstream_read_u16_be(s, &v)) return NULL;
            return mcp_short_tag_new((int16_t)v);
        }
        case MCP_TAG_INT: {
            uint32_t v;
            if (le ? mcp_bstream_read_u32_le(s, &v) : mcp_bstream_read_u32_be(s, &v)) return NULL;
            return mcp_int_tag_new((int32_t)v);
        }
        case MCP_TAG_LONG: {
            uint64_t v;
            if (le ? mcp_bstream_read_u64_le(s, &v) : mcp_bstream_read_u64_be(s, &v)) return NULL;
            return mcp_long_tag_new((int64_t)v);
        }
        case MCP_TAG_FLOAT: {
            float v;
            if (le ? mcp_bstream_read_f32_le(s, &v) : mcp_bstream_read_f32_be(s, &v)) return NULL;
            return mcp_float_tag_new(v);
        }
        case MCP_TAG_DOUBLE: {
            double v;
            if (le ? mcp_bstream_read_f64_le(s, &v) : mcp_bstream_read_f64_be(s, &v)) return NULL;
            return mcp_double_tag_new(v);
        }
        case MCP_TAG_BYTE_ARRAY: {
            uint32_t len;
            if (le ? mcp_bstream_read_u32_le(s, &len) : mcp_bstream_read_u32_be(s, &len)) return NULL;
            if (mcp_bstream_remaining(s) < len) return NULL;
            uint8_t* data = (uint8_t*)malloc(len ? len : 1);
            if (!data) return NULL;
            if (len) mcp_bstream_read_bytes(s, data, len);
            mcp_tag_t* t = mcp_byte_array_tag_new(data, len);
            free(data);
            return t;
        }
        case MCP_TAG_STRING: {
            uint16_t len;
            if (le ? mcp_bstream_read_u16_le(s, &len) : mcp_bstream_read_u16_be(s, &len)) return NULL;
            if (mcp_bstream_remaining(s) < len) return NULL;
            char* str = (char*)malloc(len + 1);
            if (!str) return NULL;
            if (len) mcp_bstream_read_bytes(s, str, len);
            str[len] = '\0';
            mcp_tag_t* t = mcp_string_tag_new(str, len);
            free(str);
            return t;
        }
        case MCP_TAG_LIST: {
            uint8_t et;
            if (mcp_bstream_read_u8(s, &et) != 0) return NULL;
            uint32_t count;
            if (le ? mcp_bstream_read_u32_le(s, &count) : mcp_bstream_read_u32_be(s, &count)) return NULL;
            mcp_tag_t* list = mcp_list_tag_new((mcp_tag_type_t)et);
            if (et == MCP_TAG_END || count == 0) {
                // Empty list; nothing more to read.
                return list;
            }
            for (uint32_t i = 0; i < count; ++i) {
                mcp_tag_t* child = mcp_nbt_read_payload(s, (mcp_tag_type_t)et, le);
                if (!child) { mcp_tag_destroy(list); return NULL; }
                if (mcp_list_tag_append(list, child) != 0) {
                    mcp_tag_destroy(child);
                    mcp_tag_destroy(list);
                    return NULL;
                }
            }
            return list;
        }
        case MCP_TAG_COMPOUND: {
            mcp_tag_t* comp = mcp_compound_tag_new();
            while (true) {
                uint8_t childType;
                if (mcp_bstream_read_u8(s, &childType) != 0) {
                    mcp_tag_destroy(comp);
                    return NULL;
                }
                if (childType == MCP_TAG_END) break;
                size_t nameLen;
                char* name = read_name(s, le, &nameLen);
                if (!name) { mcp_tag_destroy(comp); return NULL; }
                mcp_tag_t* child = mcp_nbt_read_payload(s, (mcp_tag_type_t)childType, le);
                if (!child) { free(name); mcp_tag_destroy(comp); return NULL; }
                mcp_tag_set_name(child, name, nameLen);
                mcp_compound_tag_set(comp, name, child);
                free(name);
            }
            return comp;
        }
        case MCP_TAG_INT_ARRAY: {
            uint32_t count;
            if (le ? mcp_bstream_read_u32_le(s, &count) : mcp_bstream_read_u32_be(s, &count)) return NULL;
            int32_t* data = (int32_t*)malloc((count ? count : 1) * sizeof(int32_t));
            if (!data) return NULL;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t v;
                if (le ? mcp_bstream_read_u32_le(s, &v) : mcp_bstream_read_u32_be(s, &v)) {
                    free(data); return NULL;
                }
                data[i] = (int32_t)v;
            }
            mcp_tag_t* t = mcp_int_array_tag_new(data, count);
            free(data);
            return t;
        }
        case MCP_TAG_END:
        default:
            return NULL;
    }
}

int mcp_nbt_read_named_child_header(mcp_bstream_t* s, bool le,
                                      mcp_tag_type_t* out_type,
                                      char** out_name) {
    uint8_t t;
    if (mcp_bstream_read_u8(s, &t) != 0) return -1;
    if (t == MCP_TAG_END) {
        if (out_type) *out_type = MCP_TAG_END;
        if (out_name) *out_name = NULL;
        return +1;
    }
    size_t nameLen;
    char* name = read_name(s, le, &nameLen);
    if (!name) return -1;
    if (out_type) *out_type = (mcp_tag_type_t)t;
    if (out_name) *out_name = name; else free(name);
    return 0;
}

mcp_tag_t* mcp_nbt_read_root(mcp_bstream_t* s, bool le) {
    uint8_t type;
    if (mcp_bstream_read_u8(s, &type) != 0) return NULL;
    if (type == MCP_TAG_END) return mcp_end_tag_new();
    size_t nameLen;
    char* name = read_name(s, le, &nameLen);
    if (!name) return NULL;
    mcp_tag_t* t = mcp_nbt_read_payload(s, (mcp_tag_type_t)type, le);
    if (!t) { free(name); return NULL; }
    mcp_tag_set_name(t, name, nameLen);
    free(name);
    return t;
}

void mcp_nbt_write_root(mcp_bstream_t* s, const mcp_tag_t* root, bool le) {
    if (!root) {
        mcp_bstream_write_u8(s, (uint8_t)MCP_TAG_END);
        return;
    }
    mcp_bstream_write_u8(s, (uint8_t)root->type);
    if (root->name) {
        if (le) mcp_bstream_write_u16_le(s, (uint16_t)root->name_len);
        else    mcp_bstream_write_u16_be(s, (uint16_t)root->name_len);
        mcp_bstream_write_bytes(s, root->name, root->name_len);
    } else {
        if (le) mcp_bstream_write_u16_le(s, 0);
        else    mcp_bstream_write_u16_be(s, 0);
    }
    if (root->vtable && root->vtable->write_payload) {
        root->vtable->write_payload(root, s, le);
    }
}

mcp_tag_t* mcp_nbt_read_bytes(const uint8_t* data, size_t len, bool le) {
    mcp_bstream_t s;
    mcp_bstream_init_read(&s, data, len);
    mcp_tag_t* t = mcp_nbt_read_root(&s, le);
    mcp_bstream_free(&s);
    return t;
}

uint8_t* mcp_nbt_write_bytes(const mcp_tag_t* root, bool le, size_t* out_len) {
    mcp_bstream_t s;
    mcp_bstream_init(&s);
    mcp_nbt_write_root(&s, root, le);
    uint8_t* out = (uint8_t*)malloc(s.size ? s.size : 1);
    if (!out) { mcp_bstream_free(&s); return NULL; }
    if (s.size) memcpy(out, s.data, s.size);
    if (out_len) *out_len = s.size;
    mcp_bstream_free(&s);
    return out;
}
