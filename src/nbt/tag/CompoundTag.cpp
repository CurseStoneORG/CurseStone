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
// CompoundTag.cpp — NBT TAG_Compound.
#include "CompoundTag.h"
#include "ByteTag.h"
#include "DoubleTag.h"
#include "EndTag.h"
#include "FloatTag.h"
#include "IntTag.h"
#include "LongTag.h"
#include "ShortTag.h"
#include "StringTag.h"
#include "TagDefs.h"

mcp_compound_tag::~mcp_compound_tag() {
    for (const mcp_compound_entry& e : entries)
        delete e.tag;
}

mcp_tag_t* mcp_compound_tag_new(void) {
    try {
        return new mcp_compound_tag();
    } catch (...) {
        return NULL;
    }
}

mcp_tag_t* mcp_compound_tag_new_named(const char* name) {
    mcp_tag_t* t = mcp_compound_tag_new();
    if (t)
        mcp_tag_set_name_cstr(t, name);
    return t;
}

int mcp_compound_tag_set(mcp_tag_t* self, const char* name, mcp_tag_t* child) {
    if (!self || self->type != MCP_TAG_COMPOUND || !name || !child)
        return -1;
    auto* t = static_cast<mcp_compound_tag*>(self);
    for (mcp_compound_entry& e : t->entries) {
        if (e.name == name) {
            // Replace.
            delete e.tag;
            e.tag = child;
            return 0;
        }
    }
    try {
        t->entries.emplace_back(name, child);
    } catch (...) {
        return -1; // child stays with the caller
    }
    return 0;
}

int mcp_compound_tag_set_byte(mcp_tag_t* s, const char* n, int8_t v) {
    if (!s)
        return -1;
    return mcp_compound_tag_set(s, n, mcp_byte_tag_new_named(n, v));
}
int mcp_compound_tag_set_short(mcp_tag_t* s, const char* n, int16_t v) {
    if (!s)
        return -1;
    return mcp_compound_tag_set(s, n, mcp_short_tag_new_named(n, v));
}
int mcp_compound_tag_set_int(mcp_tag_t* s, const char* n, int32_t v) {
    if (!s)
        return -1;
    return mcp_compound_tag_set(s, n, mcp_int_tag_new_named(n, v));
}
int mcp_compound_tag_set_long(mcp_tag_t* s, const char* n, int64_t v) {
    if (!s)
        return -1;
    return mcp_compound_tag_set(s, n, mcp_long_tag_new_named(n, v));
}
int mcp_compound_tag_set_float(mcp_tag_t* s, const char* n, float v) {
    if (!s)
        return -1;
    return mcp_compound_tag_set(s, n, mcp_float_tag_new_named(n, v));
}
int mcp_compound_tag_set_double(mcp_tag_t* s, const char* n, double v) {
    if (!s)
        return -1;
    return mcp_compound_tag_set(s, n, mcp_double_tag_new_named(n, v));
}
int mcp_compound_tag_set_string(mcp_tag_t* s, const char* n, const char* v) {
    if (!s)
        return -1;
    return mcp_compound_tag_set(s, n, mcp_string_tag_new_named_cstr(n, v));
}

const mcp_tag_t* mcp_compound_tag_get(const mcp_tag_t* self, const char* name) {
    if (!self || self->type != MCP_TAG_COMPOUND || !name)
        return NULL;
    for (const mcp_compound_entry& e : static_cast<const mcp_compound_tag*>(self)->entries) {
        if (e.name == name)
            return e.tag;
    }
    return NULL;
}

int8_t mcp_compound_tag_get_byte(const mcp_tag_t* s, const char* n, int8_t d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_BYTE)
        return d;
    return static_cast<const mcp_byte_tag*>(t)->value;
}
int16_t mcp_compound_tag_get_short(const mcp_tag_t* s, const char* n, int16_t d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_SHORT)
        return d;
    return static_cast<const mcp_short_tag*>(t)->value;
}
int32_t mcp_compound_tag_get_int(const mcp_tag_t* s, const char* n, int32_t d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_INT)
        return d;
    return static_cast<const mcp_int_tag*>(t)->value;
}
int64_t mcp_compound_tag_get_long(const mcp_tag_t* s, const char* n, int64_t d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_LONG)
        return d;
    return static_cast<const mcp_long_tag*>(t)->value;
}
float mcp_compound_tag_get_float(const mcp_tag_t* s, const char* n, float d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_FLOAT)
        return d;
    return static_cast<const mcp_float_tag*>(t)->value;
}
double mcp_compound_tag_get_double(const mcp_tag_t* s, const char* n, double d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_DOUBLE)
        return d;
    return static_cast<const mcp_double_tag*>(t)->value;
}
const char* mcp_compound_tag_get_string(const mcp_tag_t* s, const char* n, const char* d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_STRING)
        return d ? d : "";
    // This function MUST NOT return NULL. Almost every caller feeds the
    // result straight into a std::string, and std::string(NULL) is
    // undefined behaviour. c_str() never returns NULL, so the accessor
    // stays total. An empty stored value is returned as "" (not as the
    // caller's default), matching the historical behaviour.
    (void)d;
    return static_cast<const mcp_string_tag*>(t)->value.c_str();
}

const mcp_tag_t* mcp_compound_tag_get_compound(const mcp_tag_t* s, const char* n) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_COMPOUND)
        return NULL;
    return t;
}

size_t mcp_compound_tag_size(const mcp_tag_t* self) {
    if (!self || self->type != MCP_TAG_COMPOUND)
        return 0;
    return static_cast<const mcp_compound_tag*>(self)->entries.size();
}

bool mcp_compound_tag_contains(const mcp_tag_t* self, const char* name) {
    return mcp_compound_tag_get(self, name) != NULL;
}

bool mcp_compound_tag_remove(mcp_tag_t* self, const char* name) {
    if (!self || self->type != MCP_TAG_COMPOUND || !name)
        return false;
    auto* t = static_cast<mcp_compound_tag*>(self);
    for (auto it = t->entries.begin(); it != t->entries.end(); ++it) {
        if (it->name == name) {
            delete it->tag;
            t->entries.erase(it); // shift keeps the survivors' order intact
            return true;
        }
    }
    return false;
}

void mcp_compound_tag::write_payload(mcp_bstream_t* s, bool le) const {
    // For each child: type byte, name (uint16 length + bytes), payload.
    for (const mcp_compound_entry& e : entries) {
        mcp_bstream_write_u8(s, (uint8_t)e.tag->type);
        if (le)
            mcp_bstream_write_u16_le(s, (uint16_t)e.name.size());
        else
            mcp_bstream_write_u16_be(s, (uint16_t)e.name.size());
        if (!e.name.empty())
            mcp_bstream_write_bytes(s, e.name.data(), e.name.size());
        e.tag->write_payload(s, le);
    }
    // End marker: TAG_End (0 byte, no name).
    mcp_bstream_write_u8(s, (uint8_t)MCP_TAG_END);
}
