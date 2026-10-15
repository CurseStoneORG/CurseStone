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
// Tag.c — base tag helpers. Pure C.
#include "Tag.h"

#include <stdlib.h>
#include <string.h>

void mcp_tag_destroy(mcp_tag_t* tag) {
    if (!tag)
        return;
    if (tag->vtable && tag->vtable->destroy) {
        tag->vtable->destroy(tag);
    } else {
        if (tag->name)
            free(tag->name);
        free(tag);
    }
}

mcp_tag_type_t mcp_tag_get_type(const mcp_tag_t* tag) {
    return tag ? tag->type : MCP_TAG_END;
}

void mcp_tag_set_name(mcp_tag_t* tag, const char* name, size_t len) {
    if (!tag)
        return;
    if (tag->name) {
        free(tag->name);
        tag->name = NULL;
        tag->name_len = 0;
    }
    if (!name || len == 0)
        return;
    tag->name = (char*)malloc(len + 1);
    if (!tag->name)
        return;
    memcpy(tag->name, name, len);
    tag->name[len] = '\0';
    tag->name_len = len;
}

void mcp_tag_set_name_cstr(mcp_tag_t* tag, const char* name) {
    mcp_tag_set_name(tag, name, name ? strlen(name) : 0);
}

const char* mcp_tag_type_name(mcp_tag_type_t t) {
    switch (t) {
    case MCP_TAG_END:
        return "TAG_End";
    case MCP_TAG_BYTE:
        return "TAG_Byte";
    case MCP_TAG_SHORT:
        return "TAG_Short";
    case MCP_TAG_INT:
        return "TAG_Int";
    case MCP_TAG_LONG:
        return "TAG_Long";
    case MCP_TAG_FLOAT:
        return "TAG_Float";
    case MCP_TAG_DOUBLE:
        return "TAG_Double";
    case MCP_TAG_BYTE_ARRAY:
        return "TAG_Byte_Array";
    case MCP_TAG_STRING:
        return "TAG_String";
    case MCP_TAG_LIST:
        return "TAG_List";
    case MCP_TAG_COMPOUND:
        return "TAG_Compound";
    case MCP_TAG_INT_ARRAY:
        return "TAG_Int_Array";
    }
    return "TAG_Unknown";
}
