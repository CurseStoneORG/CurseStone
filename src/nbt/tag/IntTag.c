// SPDX-License-Identifier: LGPL-3.0-or-later
// IntTag.c. Pure C.
#include "IntTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"
#include <stdlib.h>
#include <stdio.h>

static void int_destroy(mcp_tag_t* self) {
    if (!self) return;
    if (self->name) free(self->name);
    free(self);
}
static void int_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_int_tag_t* t = (mcp_int_tag_t*)self;
    if (le) mcp_bstream_write_u32_le((mcp_bstream_t*)stream, (uint32_t)t->value);
    else    mcp_bstream_write_u32_be((mcp_bstream_t*)stream, (uint32_t)t->value);
}
static void int_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%d", (int)((mcp_int_tag_t*)self)->value);
}
static const struct mcp_tag_vtable INT_VT = { int_destroy, int_write, int_to_string };

mcp_tag_t* mcp_int_tag_new(int32_t v) {
    mcp_int_tag_t* t = (mcp_int_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &INT_VT;
    t->header.type   = MCP_TAG_INT;
    t->value = v;
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_int_tag_new_named(const char* name, int32_t v) {
    mcp_tag_t* t = mcp_int_tag_new(v);
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}
