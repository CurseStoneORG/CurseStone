// SPDX-License-Identifier: LGPL-3.0-or-later
// ListTag.c. Pure C.
#include "ListTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"
#include "../../utils/Logger.h"

#include <stdlib.h>
#include <string.h>

static void list_destroy(mcp_tag_t* self) {
    if (!self) return;
    mcp_list_tag_t* t = (mcp_list_tag_t*)self;
    if (t->children) {
        for (size_t i = 0; i < t->count; ++i) {
            mcp_tag_destroy(t->children[i]);
        }
        free(t->children);
    }
    if (self->name) free(self->name);
    free(self);
}
static void list_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_list_tag_t* t = (mcp_list_tag_t*)self;
    mcp_bstream_t* s = (mcp_bstream_t*)stream;
    // 1-byte element type id, then signed int32 length, then each
    // child's payload (without their own type byte or name).
    mcp_bstream_write_u8(s, (uint8_t)t->element_type);
    if (le) mcp_bstream_write_u32_le(s, (uint32_t)t->count);
    else    mcp_bstream_write_u32_be(s, (uint32_t)t->count);
    for (size_t i = 0; i < t->count; ++i) {
        if (t->children[i]->vtable && t->children[i]->vtable->write_payload) {
            t->children[i]->vtable->write_payload(t->children[i], s, le);
        }
    }
}
static void list_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    (void)self;
    if (out && out_size) out[0] = '\0';
}
static const struct mcp_tag_vtable LIST_VT = { list_destroy, list_write, list_to_string };

mcp_tag_t* mcp_list_tag_new(mcp_tag_type_t element_type) {
    mcp_list_tag_t* t = (mcp_list_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &LIST_VT;
    t->header.type   = MCP_TAG_LIST;
    t->element_type  = element_type;
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_list_tag_new_named(const char* name, mcp_tag_type_t element_type) {
    mcp_tag_t* t = mcp_list_tag_new(element_type);
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}

int mcp_list_tag_append(mcp_tag_t* self, mcp_tag_t* child) {
    if (!self || !child) return -1;
    mcp_list_tag_t* t = (mcp_list_tag_t*)self;
    // Enforce element type homogeneity.
    if (child->type != t->element_type) {
        // Allow upgrading from TAG_END (empty list) to whatever the
        // first appended child is.
        if (t->element_type == MCP_TAG_END) {
            t->element_type = child->type;
        } else {
            return -1;  // type mismatch
        }
    }
    if (t->count == t->capacity) {
        size_t newCap = t->capacity == 0 ? 4 : t->capacity * 2;
        mcp_tag_t** arr = (mcp_tag_t**)realloc(t->children,
                                                newCap * sizeof(mcp_tag_t*));
        if (!arr) return -1;
        t->children = arr;
        t->capacity = newCap;
    }
    t->children[t->count++] = child;
    return 0;
}

size_t mcp_list_tag_size(const mcp_tag_t* self) {
    if (!self || self->type != MCP_TAG_LIST) return 0;
    return ((const mcp_list_tag_t*)self)->count;
}
const mcp_tag_t* mcp_list_tag_get(const mcp_tag_t* self, size_t index) {
    if (!self || self->type != MCP_TAG_LIST) return NULL;
    const mcp_list_tag_t* t = (const mcp_list_tag_t*)self;
    if (index >= t->count) return NULL;
    return t->children[index];
}
