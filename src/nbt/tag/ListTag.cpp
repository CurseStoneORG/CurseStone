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
#include "TagDefs.h"

mcp_list_tag::~mcp_list_tag() {
    for (mcp_tag_t* c : children)
        delete c;
}

mcp_tag_t* mcp_list_tag_new(mcp_tag_type_t element_type) {
    try {
        return new mcp_list_tag(element_type);
    } catch (...) {
        return NULL;
    }
}

mcp_tag_t* mcp_list_tag_new_named(const char* name, mcp_tag_type_t element_type) {
    mcp_tag_t* t = mcp_list_tag_new(element_type);
    if (t)
        mcp_tag_set_name_cstr(t, name);
    return t;
}

int mcp_list_tag_append(mcp_tag_t* self, mcp_tag_t* child) {
    if (!self || self->type != MCP_TAG_LIST || !child)
        return -1;
    auto* t = static_cast<mcp_list_tag*>(self);
    // Enforce element type homogeneity.
    if (child->type != t->element_type) {
        // Allow upgrading from TAG_END (empty list) to whatever the
        // first appended child is.
        if (t->element_type == MCP_TAG_END) {
            t->element_type = child->type;
        } else {
            return -1; // type mismatch
        }
    }
    try {
        t->children.push_back(child);
    } catch (...) {
        return -1; // child stays with the caller
    }
    return 0;
}

size_t mcp_list_tag_size(const mcp_tag_t* self) {
    if (!self || self->type != MCP_TAG_LIST)
        return 0;
    return static_cast<const mcp_list_tag*>(self)->children.size();
}

const mcp_tag_t* mcp_list_tag_get(const mcp_tag_t* self, size_t index) {
    if (!self || self->type != MCP_TAG_LIST)
        return NULL;
    const auto* t = static_cast<const mcp_list_tag*>(self);
    if (index >= t->children.size())
        return NULL;
    return t->children[index];
}

void mcp_list_tag::write_payload(mcp_bstream_t* s, bool le) const {
    // 1-byte element type id, then signed int32 length, then each
    // child's payload (without their own type byte or name).
    mcp_bstream_write_u8(s, (uint8_t)element_type);
    if (le)
        mcp_bstream_write_u32_le(s, (uint32_t)children.size());
    else
        mcp_bstream_write_u32_be(s, (uint32_t)children.size());
    for (const mcp_tag_t* c : children)
        c->write_payload(s, le);
}
