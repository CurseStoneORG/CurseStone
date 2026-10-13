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
// ByteArrayTag.c. Pure C.
#include "ByteArrayTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"
#include <stdlib.h>
#include <string.h>

static void ba_destroy(mcp_tag_t* self) {
    if (!self) return;
    mcp_byte_array_tag_t* t = (mcp_byte_array_tag_t*)self;
    if (t->data) free(t->data);
    if (self->name) free(self->name);
    free(self);
}
static void ba_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_byte_array_tag_t* t = (mcp_byte_array_tag_t*)self;
    mcp_bstream_t* s = (mcp_bstream_t*)stream;
    if (le) mcp_bstream_write_u32_le(s, (uint32_t)t->length);
    else    mcp_bstream_write_u32_be(s, (uint32_t)t->length);
    mcp_bstream_write_bytes(s, t->data, t->length);
}
static void ba_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    (void)self;
    if (out && out_size) out[0] = '\0';
}
static const struct mcp_tag_vtable BA_VT = { ba_destroy, ba_write, ba_to_string };

mcp_tag_t* mcp_byte_array_tag_new(const uint8_t* data, size_t len) {
    mcp_byte_array_tag_t* t = (mcp_byte_array_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &BA_VT;
    t->header.type   = MCP_TAG_BYTE_ARRAY;
    t->length = len;
    if (len > 0) {
        t->data = (uint8_t*)malloc(len);
        if (!t->data) { free(t); return NULL; }
        memcpy(t->data, data, len);
    }
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_byte_array_tag_new_named(const char* name, const uint8_t* data, size_t len) {
    mcp_tag_t* t = mcp_byte_array_tag_new(data, len);
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}
