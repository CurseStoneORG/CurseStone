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
// ListTag — NBT TAG_List (homogeneous children, no per-child names).
#pragma once

#include <cstddef>
#include <vector>

#include "Tag.h"

namespace cursestone::nbt {

class ListTag : public Tag {
  public:
    explicit ListTag(TagType element_type) : Tag(TAG_LIST), element_type(element_type) {}
    ListTag(const char* name, TagType element_type) : Tag(TAG_LIST), element_type(element_type) {
        if (name)
            this->name = name;
    }
    ~ListTag() override;

    TagType element_type;
    std::vector<Tag*> children; // owned, destroyed together with the list

    void write_payload(mcp_bstream_t* s, bool le) const override;
};

// Append a child. The list takes ownership of `child` on success
// (returns true); on failure the caller keeps ownership.
bool append(Tag* list, Tag* child);

// Number of children (0 for a non-list or missing tag).
size_t size(const Tag* list);

// Child at `index`, nullptr when out of range or not a list.
const Tag* at(const Tag* list, size_t index);

} // namespace cursestone::nbt
