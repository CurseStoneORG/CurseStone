// SPDX-License-Identifier: LGPL-3.0-or-later
// StringTag.c. Pure C.
#include "StringTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void str_destroy(mcp_tag_t* self) {
    if (!self) return;
    mcp_string_tag_t* t = (mcp_string_tag_t*)self;
    if (t->value) free(t->value);
    if (self->name) free(self->name);
    free(self);
}
static void str_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_string_tag_t* t = (mcp_string_tag_t*)self;
    mcp_bstream_t* s = (mcp_bstream_t*)stream;
    if (le) {
        // MCPE strings use uint16 little-endian prefix
        mcp_bstream_write_u16_le(s, (uint16_t)t->length);
    } else {
        // Java NBT strings use uint16 big-endian prefix
        mcp_bstream_write_u16_be(s, (uint16_t)t->length);
    }
    if (t->value && t->length) mcp_bstream_write_bytes(s, t->value, t->length);
}
static void str_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    mcp_string_tag_t* t = (mcp_string_tag_t*)self;
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%s", t->value ? t->value : "");
}
static const struct mcp_tag_vtable STR_VT = { str_destroy, str_write, str_to_string };

mcp_tag_t* mcp_string_tag_new(const char* value, size_t len) {
    mcp_string_tag_t* t = (mcp_string_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &STR_VT;
    t->header.type   = MCP_TAG_STRING;
    t->length = len;
    // Always allocate, even for an empty string. calloc() leaves ->value as
    // NULL when len == 0, and every getter then hands that NULL back to the
    // caller: building a std::string from it calls strlen(NULL) and
    // segfaults. That is exactly what killed the server when a player
    // placed a sign, typed on ONE line and left the other three blank — the
    // blank lines came back as NULL (see mcp_compound_tag_get_string).
    // An empty string tag must still hold a valid "" pointer.
    t->value = (char*)malloc(len + 1);
    if (!t->value) { free(t); return NULL; }
    if (len) memcpy(t->value, value, len);
    t->value[len] = '\0';
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_string_tag_new_cstr(const char* value) {
    return mcp_string_tag_new(value, value ? strlen(value) : 0);
}
mcp_tag_t* mcp_string_tag_new_named_cstr(const char* name, const char* value) {
    mcp_tag_t* t = mcp_string_tag_new_cstr(value);
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}
const char* mcp_string_tag_get(const mcp_tag_t* tag) {
    if (!tag || tag->type != MCP_TAG_STRING) return NULL;
    // Never hand back NULL for a stored string: an empty tag still holds a
    // valid "" (mcp_string_tag_new always allocates).
    const char* v = ((const mcp_string_tag_t*)tag)->value;
    return v ? v : "";
}
