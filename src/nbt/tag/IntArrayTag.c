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
// IntArrayTag.c. Pure C.
#include "IntArrayTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"
#include <stdlib.h>
#include <string.h>

static void ia_destroy(mcp_tag_t* self) {
    if (!self)
        return;
    mcp_int_array_tag_t* t = (mcp_int_array_tag_t*)self;
    if (t->data)
        free(t->data);
    if (self->name)
        free(self->name);
    free(self);
}
static void ia_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_int_array_tag_t* t = (mcp_int_array_tag_t*)self;
    mcp_bstream_t* s = (mcp_bstream_t*)stream;
    if (le)
        mcp_bstream_write_u32_le(s, (uint32_t)t->count);
    else
        mcp_bstream_write_u32_be(s, (uint32_t)t->count);
    for (size_t i = 0; i < t->count; ++i) {
        if (le)
            mcp_bstream_write_u32_le(s, (uint32_t)t->data[i]);
        else
            mcp_bstream_write_u32_be(s, (uint32_t)t->data[i]);
    }
}
static void ia_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    (void)self;
    if (out && out_size)
        out[0] = '\0';
}
static const struct mcp_tag_vtable IA_VT = {ia_destroy, ia_write, ia_to_string};

mcp_tag_t* mcp_int_array_tag_new(const int32_t* data, size_t count) {
    mcp_int_array_tag_t* t = (mcp_int_array_tag_t*)calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->header.vtable = &IA_VT;
    t->header.type = MCP_TAG_INT_ARRAY;
    t->count = count;
    if (count > 0) {
        t->data = (int32_t*)malloc(count * sizeof(int32_t));
        if (!t->data) {
            free(t);
            return NULL;
        }
        memcpy(t->data, data, count * sizeof(int32_t));
    }
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_int_array_tag_new_named(const char* name, const int32_t* data, size_t count) {
    mcp_tag_t* t = mcp_int_array_tag_new(data, count);
    if (t)
        mcp_tag_set_name_cstr(t, name);
    return t;
}
