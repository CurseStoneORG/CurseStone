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
// CompoundTag — ordered map of name→tag. The most common NBT container:
// every chunk's "Level" root, every block entity, every player.dat file
// is a CompoundTag.
#pragma once

#include <string>
#include <vector>

#include "Tag.h"

namespace cursestone::nbt {

struct CompoundEntry {
    CompoundEntry(std::string n, Tag* t) : name(std::move(n)), tag(t) {}
    std::string name;
    Tag* tag; // owned
};

class CompoundTag : public Tag {
  public:
    explicit CompoundTag(const char* name = nullptr) : Tag(TAG_COMPOUND) {
        if (name)
            this->name = name;
    }
    ~CompoundTag() override;

    std::vector<CompoundEntry> entries; // insertion order preserved (wire format)

    void write_payload(mcp_bstream_t* s, bool le) const override;
};

// The compound/list operations below all tolerate a missing or
// wrongly-typed `c` so callers can chain lookups without null checks.

// Insert or replace a child. The compound takes ownership of `child`
// on success; on failure the caller keeps ownership.
bool set(Tag* c, const char* name, Tag* child);

// Convenience setters for primitives.
bool set_byte(Tag* c, const char* name, int8_t v);
bool set_short(Tag* c, const char* name, int16_t v);
bool set_int(Tag* c, const char* name, int32_t v);
bool set_long(Tag* c, const char* name, int64_t v);
bool set_float(Tag* c, const char* name, float v);
bool set_double(Tag* c, const char* name, double v);
bool set_string(Tag* c, const char* name, const char* v);

// Lookup. Returns nullptr if missing.
const Tag* get(const Tag* c, const char* name);

// Convenience getters (default if missing or wrong type).
int8_t get_byte(const Tag* c, const char* name, int8_t def);
int16_t get_short(const Tag* c, const char* name, int16_t def);
int32_t get_int(const Tag* c, const char* name, int32_t def);
int64_t get_long(const Tag* c, const char* name, int64_t def);
float get_float(const Tag* c, const char* name, float def);
double get_double(const Tag* c, const char* name, double def);

// Never returns nullptr: an empty stored value wins over the default,
// and almost every caller feeds the result straight into a std::string
// (std::string(nullptr) is undefined behaviour).
const char* get_string(const Tag* c, const char* name, const char* def);

// Returns the named child when it is itself a compound (nullptr otherwise).
const Tag* get_compound(const Tag* c, const char* name);

bool contains(const Tag* c, const char* name);

// Remove `name` and destroy its tag. Returns true when something was
// actually removed (so a caller can use it as a conditional rewrite
// without knowing whether the key was there). The order of the
// remaining entries is preserved.
bool remove(Tag* c, const char* name);

} // namespace cursestone::nbt
