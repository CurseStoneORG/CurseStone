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
// ByteTag.c. Pure C.
#include "ByteTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void byte_destroy(mcp_tag_t* self) {
    if (!self)
        return;
    if (self->name)
        free(self->name);
    free(self);
}
static void byte_write(const mcp_tag_t* self, void* stream, bool le) {
    (void)le;
    mcp_byte_tag_t* t = (mcp_byte_tag_t*)self;
    mcp_bstream_write_i8((mcp_bstream_t*)stream, t->value);
}
static void byte_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    if (!out || out_size == 0)
        return;
    snprintf(out, out_size, "%d", (int)((mcp_byte_tag_t*)self)->value);
}
static const struct mcp_tag_vtable BYTE_VT = {byte_destroy, byte_write, byte_to_string};

mcp_tag_t* mcp_byte_tag_new(int8_t v) {
    mcp_byte_tag_t* t = (mcp_byte_tag_t*)calloc(1, sizeof(mcp_byte_tag_t));
    if (!t)
        return NULL;
    t->header.vtable = &BYTE_VT;
    t->header.type = MCP_TAG_BYTE;
    t->value = v;
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_byte_tag_new_named(const char* name, int8_t v) {
    mcp_tag_t* t = mcp_byte_tag_new(v);
    if (t)
        mcp_tag_set_name_cstr(t, name);
    return t;
}
