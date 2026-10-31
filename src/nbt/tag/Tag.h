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
// Tag — base NBT tag class.
//
// NBT (Named Binary Tag) is Minecraft's serialization format. Used for:
//   - Anvil chunk data, level.dat, and player .dat files
//   - Block entity (tile) data: signs, chests, furnaces
//   - Item NBT data (enchantments, custom names, durability)
//
// Endianness:
//   - Java/NBT big-endian (Anvil chunks, level.dat, player .dat)
//   - MCPE little-endian (network packets, item extra data)
//
// The tree is a plain owning-pointer hierarchy: every tag returned by
// `new` is owned by the caller (or by the compound/list that stored it)
// and is released with `delete`, which recursively destroys children.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "../../utils/BinaryStream.h"

namespace cursestone::nbt {

// Tag type ids. The numeric values are the wire format.
enum TagType {
    TAG_END = 0,
    TAG_BYTE = 1,
    TAG_SHORT = 2,
    TAG_INT = 3,
    TAG_LONG = 4,
    TAG_FLOAT = 5,
    TAG_DOUBLE = 6,
    TAG_BYTE_ARRAY = 7,
    TAG_STRING = 8,
    TAG_LIST = 9,
    TAG_COMPOUND = 10,
    TAG_INT_ARRAY = 11,
};

class Tag {
  public:
    explicit Tag(TagType t) : type(t) {}
    virtual ~Tag() = default;
    Tag(const Tag&) = delete;
    Tag& operator=(const Tag&) = delete;

    const TagType type;
    std::string name; // empty = unnamed (list elements)

    // Serialize this tag's payload (NOT the leading type byte or name).
    virtual void write_payload(mcp_bstream_t* s, bool le) const {
        (void)s;
        (void)le;
    }
};

// Type of `t`, TAG_END for nullptr (so callers can chain lookups without
// a null check first).
inline TagType type_of(const Tag* t) {
    return t ? t->type : TAG_END;
}

// Assign the name. A null pointer or zero length clears it; on
// allocation failure the previous name is kept.
inline void set_name(Tag* t, const char* name, size_t len) {
    if (!t)
        return;
    if (!name || len == 0) {
        t->name.clear();
        return;
    }
    try {
        t->name.assign(name, len);
    } catch (...) {
        // Out of memory: keep the previous name.
    }
}

// Convenience overload for NUL-terminated names.
inline void set_name(Tag* t, const char* name) {
    set_name(t, name, name ? std::strlen(name) : 0u);
}

} // namespace cursestone::nbt
