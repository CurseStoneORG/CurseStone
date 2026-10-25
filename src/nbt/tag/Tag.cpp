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
// Tag.cpp — base tag free functions.
#include "Tag.h"
#include "TagDefs.h"

#include <cstring>

void mcp_tag_destroy(mcp_tag_t* tag) {
    delete tag;
}

mcp_tag_type_t mcp_tag_get_type(const mcp_tag_t* tag) {
    return tag ? tag->type : MCP_TAG_END;
}

void mcp_tag_set_name(mcp_tag_t* tag, const char* name, size_t len) {
    if (!tag)
        return;
    if (!name || len == 0) {
        tag->name.clear();
        return;
    }
    try {
        tag->name.assign(name, len);
    } catch (...) {
        // Out of memory: keep the previous name, like the C version did.
    }
}

void mcp_tag_set_name_cstr(mcp_tag_t* tag, const char* name) {
    mcp_tag_set_name(tag, name, name ? strlen(name) : 0);
}
