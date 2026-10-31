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
#include "FloatTag.h"
#include "IntTag.h"
#include "LongTag.h"
#include "ShortTag.h"
#include "StringTag.h"

namespace cursestone::nbt {

CompoundTag::~CompoundTag() {
    for (const CompoundEntry& e : entries)
        delete e.tag;
}

bool set(Tag* self, const char* name, Tag* child) {
    if (!self || self->type != TAG_COMPOUND || !name || !child)
        return false;
    auto* t = static_cast<CompoundTag*>(self);
    for (CompoundEntry& e : t->entries) {
        if (e.name == name) {
            // Replace.
            delete e.tag;
            e.tag = child;
            return true;
        }
    }
    try {
        t->entries.emplace_back(name, child);
    } catch (...) {
        return false; // child stays with the caller
    }
    return true;
}

namespace {
// Build a named child and insert it; on failure the child is destroyed
// here so the convenience setters never leak.
template <typename MakeTag>
bool set_via(Tag* self, const char* name, MakeTag make) {
    if (!self)
        return false;
    Tag* child = make();
    if (!set(self, name, child)) {
        delete child;
        return false;
    }
    return true;
}
} // namespace

bool set_byte(Tag* s, const char* n, int8_t v) {
    return set_via(s, n, [=] { return new ByteTag(n, v); });
}
bool set_short(Tag* s, const char* n, int16_t v) {
    return set_via(s, n, [=] { return new ShortTag(n, v); });
}
bool set_int(Tag* s, const char* n, int32_t v) {
    return set_via(s, n, [=] { return new IntTag(n, v); });
}
bool set_long(Tag* s, const char* n, int64_t v) {
    return set_via(s, n, [=] { return new LongTag(n, v); });
}
bool set_float(Tag* s, const char* n, float v) {
    return set_via(s, n, [=] { return new FloatTag(n, v); });
}
bool set_double(Tag* s, const char* n, double v) {
    return set_via(s, n, [=] { return new DoubleTag(n, v); });
}
bool set_string(Tag* s, const char* n, const char* v) {
    return set_via(s, n, [=] { return new StringTag(n, v); });
}

const Tag* get(const Tag* self, const char* name) {
    if (!self || self->type != TAG_COMPOUND || !name)
        return nullptr;
    for (const CompoundEntry& e : static_cast<const CompoundTag*>(self)->entries) {
        if (e.name == name)
            return e.tag;
    }
    return nullptr;
}

int8_t get_byte(const Tag* s, const char* n, int8_t d) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_BYTE)
        return d;
    return static_cast<const ByteTag*>(t)->value;
}
int16_t get_short(const Tag* s, const char* n, int16_t d) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_SHORT)
        return d;
    return static_cast<const ShortTag*>(t)->value;
}
int32_t get_int(const Tag* s, const char* n, int32_t d) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_INT)
        return d;
    return static_cast<const IntTag*>(t)->value;
}
int64_t get_long(const Tag* s, const char* n, int64_t d) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_LONG)
        return d;
    return static_cast<const LongTag*>(t)->value;
}
float get_float(const Tag* s, const char* n, float d) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_FLOAT)
        return d;
    return static_cast<const FloatTag*>(t)->value;
}
double get_double(const Tag* s, const char* n, double d) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_DOUBLE)
        return d;
    return static_cast<const DoubleTag*>(t)->value;
}

const char* get_string(const Tag* s, const char* n, const char* d) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_STRING)
        return d ? d : "";
    // This function MUST NOT return nullptr. Almost every caller feeds
    // the result straight into a std::string, and std::string(nullptr)
    // is undefined behaviour. c_str() never returns nullptr, so the
    // accessor stays total. An empty stored value is returned as ""
    // (not as the caller's default), matching the historical behaviour.
    (void)d;
    return static_cast<const StringTag*>(t)->value.c_str();
}

const Tag* get_compound(const Tag* s, const char* n) {
    const Tag* t = get(s, n);
    if (!t || t->type != TAG_COMPOUND)
        return nullptr;
    return t;
}

bool contains(const Tag* self, const char* name) {
    return get(self, name) != nullptr;
}

bool remove(Tag* self, const char* name) {
    if (!self || self->type != TAG_COMPOUND || !name)
        return false;
    auto* t = static_cast<CompoundTag*>(self);
    for (auto it = t->entries.begin(); it != t->entries.end(); ++it) {
        if (it->name == name) {
            delete it->tag;
            t->entries.erase(it); // shift keeps the survivors' order intact
            return true;
        }
    }
    return false;
}

void CompoundTag::write_payload(mcp_bstream_t* s, bool le) const {
    // For each child: type byte, name (uint16 length + bytes), payload.
    for (const CompoundEntry& e : entries) {
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
    mcp_bstream_write_u8(s, (uint8_t)TAG_END);
}

} // namespace cursestone::nbt
