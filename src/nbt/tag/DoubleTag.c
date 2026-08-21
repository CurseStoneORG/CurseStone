// SPDX-License-Identifier: LGPL-3.0-or-later
// DoubleTag.c. Pure C.
#include "DoubleTag.h"
#include "../../utils/BinaryStream.h"
#include <stdlib.h>
#include <stdio.h>

static void dbl_destroy(mcp_tag_t* self) {
    if (!self) return;
    if (self->name) free(self->name);
    free(self);
}
static void dbl_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_double_tag_t* t = (mcp_double_tag_t*)self;
    if (le) mcp_bstream_write_f64_le((mcp_bstream_t*)stream, t->value);
    else    mcp_bstream_write_f64_be((mcp_bstream_t*)stream, t->value);
}
static void dbl_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%f", ((mcp_double_tag_t*)self)->value);
}
static const struct mcp_tag_vtable DOUBLE_VT = { dbl_destroy, dbl_write, dbl_to_string };

mcp_tag_t* mcp_double_tag_new(double v) {
    mcp_double_tag_t* t = (mcp_double_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &DOUBLE_VT;
    t->header.type   = MCP_TAG_DOUBLE;
    t->value = v;
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_double_tag_new_named(const char* name, double v) {
    mcp_tag_t* t = mcp_double_tag_new(v);
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}
