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
// ListTag.cpp — NBT TAG_List.
#include "ListTag.h"

namespace cursestone::nbt {

ListTag::~ListTag() {
    for (Tag* c : children)
        delete c;
}

bool append(Tag* self, Tag* child) {
    if (!self || self->type != TAG_LIST || !child)
        return false;
    auto* t = static_cast<ListTag*>(self);
    // Enforce element type homogeneity.
    if (child->type != t->element_type) {
        // Allow upgrading from TAG_END (empty list) to whatever the
        // first appended child is.
        if (t->element_type == TAG_END) {
            t->element_type = child->type;
        } else {
            return false; // type mismatch
        }
    }
    try {
        t->children.push_back(child);
    } catch (...) {
        return false; // child stays with the caller
    }
    return true;
}

size_t size(const Tag* self) {
    if (!self || self->type != TAG_LIST)
        return 0;
    return static_cast<const ListTag*>(self)->children.size();
}

const Tag* at(const Tag* self, size_t index) {
    if (!self || self->type != TAG_LIST)
        return nullptr;
    const auto* t = static_cast<const ListTag*>(self);
    if (index >= t->children.size())
        return nullptr;
    return t->children[index];
}

void ListTag::write_payload(mcp_bstream_t* s, bool le) const {
    // 1-byte element type id, then signed int32 length, then each
    // child's payload (without their own type byte or name).
    mcp_bstream_write_u8(s, (uint8_t)element_type);
    if (le)
        mcp_bstream_write_u32_le(s, (uint32_t)children.size());
    else
        mcp_bstream_write_u32_be(s, (uint32_t)children.size());
    for (const Tag* c : children)
        c->write_payload(s, le);
}

} // namespace cursestone::nbt
