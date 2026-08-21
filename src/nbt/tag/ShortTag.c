// SPDX-License-Identifier: LGPL-3.0-or-later
// ShortTag.c. Pure C.
#include "ShortTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"
#include <stdlib.h>
#include <stdio.h>

static void short_destroy(mcp_tag_t* self) {
    if (!self) return;
    if (self->name) free(self->name);
    free(self);
}
static void short_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_short_tag_t* t = (mcp_short_tag_t*)self;
    if (le) mcp_bstream_write_u16_le((mcp_bstream_t*)stream, (uint16_t)t->value);
    else    mcp_bstream_write_u16_be((mcp_bstream_t*)stream, (uint16_t)t->value);
}
static void short_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%d", (int)((mcp_short_tag_t*)self)->value);
}
static const struct mcp_tag_vtable SHORT_VT = { short_destroy, short_write, short_to_string };

mcp_tag_t* mcp_short_tag_new(int16_t v) {
    mcp_short_tag_t* t = (mcp_short_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &SHORT_VT;
    t->header.type   = MCP_TAG_SHORT;
    t->value = v;
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_short_tag_new_named(const char* name, int16_t v) {
    mcp_tag_t* t = mcp_short_tag_new(v);
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}
