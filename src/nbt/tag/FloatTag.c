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
// FloatTag.c. Pure C.
#include "FloatTag.h"
#include "../../utils/BinaryStream.h"
#include <stdlib.h>
#include <stdio.h>

static void float_destroy(mcp_tag_t* self) {
    if (!self) return;
    if (self->name) free(self->name);
    free(self);
}
static void float_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_float_tag_t* t = (mcp_float_tag_t*)self;
    if (le) mcp_bstream_write_f32_le((mcp_bstream_t*)stream, t->value);
    else    mcp_bstream_write_f32_be((mcp_bstream_t*)stream, t->value);
}
static void float_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%f", ((mcp_float_tag_t*)self)->value);
}
static const struct mcp_tag_vtable FLOAT_VT = { float_destroy, float_write, float_to_string };

mcp_tag_t* mcp_float_tag_new(float v) {
    mcp_float_tag_t* t = (mcp_float_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &FLOAT_VT;
    t->header.type   = MCP_TAG_FLOAT;
    t->value = v;
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_float_tag_new_named(const char* name, float v) {
    mcp_tag_t* t = mcp_float_tag_new(v);
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}
