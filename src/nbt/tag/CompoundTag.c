// SPDX-License-Identifier: LGPL-3.0-or-later
// CompoundTag.c. Pure C.
#include "CompoundTag.h"
#include "ByteTag.h"
#include "ShortTag.h"
#include "IntTag.h"
#include "LongTag.h"
#include "FloatTag.h"
#include "DoubleTag.h"
#include "StringTag.h"
#include "EndTag.h"
#include "../../utils/BinaryStream.h"
#include "../../utils/Binary.h"
#include "../../utils/Logger.h"

#include <stdlib.h>
#include <string.h>

static void comp_destroy(mcp_tag_t* self) {
    if (!self) return;
    mcp_compound_tag_t* t = (mcp_compound_tag_t*)self;
    if (t->entries) {
        for (size_t i = 0; i < t->count; ++i) {
            if (t->entries[i].name) free(t->entries[i].name);
            mcp_tag_destroy(t->entries[i].tag);
        }
        free(t->entries);
    }
    if (self->name) free(self->name);
    free(self);
}
static void comp_write(const mcp_tag_t* self, void* stream, bool le) {
    mcp_compound_tag_t* t = (mcp_compound_tag_t*)self;
    mcp_bstream_t* s = (mcp_bstream_t*)stream;
    // For each child: type byte, name (uint16 length + bytes), payload.
    for (size_t i = 0; i < t->count; ++i) {
        const mcp_compound_entry_t* e = &t->entries[i];
        mcp_bstream_write_u8(s, (uint8_t)e->tag->type);
        if (le) mcp_bstream_write_u16_le(s, (uint16_t)e->name_len);
        else    mcp_bstream_write_u16_be(s, (uint16_t)e->name_len);
        mcp_bstream_write_bytes(s, e->name, e->name_len);
        if (e->tag->vtable && e->tag->vtable->write_payload) {
            e->tag->vtable->write_payload(e->tag, s, le);
        }
    }
    // End marker: TAG_End (0 byte, no name).
    mcp_bstream_write_u8(s, (uint8_t)MCP_TAG_END);
}
static void comp_to_string(const mcp_tag_t* self, char* out, size_t out_size) {
    (void)self;
    if (out && out_size) out[0] = '\0';
}
static const struct mcp_tag_vtable COMP_VT = { comp_destroy, comp_write, comp_to_string };

mcp_tag_t* mcp_compound_tag_new(void) {
    mcp_compound_tag_t* t = (mcp_compound_tag_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->header.vtable = &COMP_VT;
    t->header.type   = MCP_TAG_COMPOUND;
    return (mcp_tag_t*)t;
}
mcp_tag_t* mcp_compound_tag_new_named(const char* name) {
    mcp_tag_t* t = mcp_compound_tag_new();
    if (t) mcp_tag_set_name_cstr(t, name);
    return t;
}

static int find_entry(const mcp_compound_tag_t* t, const char* name) {
    for (size_t i = 0; i < t->count; ++i) {
        if (strcmp(t->entries[i].name, name) == 0) return (int)i;
    }
    return -1;
}

int mcp_compound_tag_set(mcp_tag_t* self, const char* name, mcp_tag_t* child) {
    if (!self || !name || !child) return -1;
    mcp_compound_tag_t* t = (mcp_compound_tag_t*)self;
    int existing = find_entry(t, name);
    if (existing >= 0) {
        // Replace.
        mcp_tag_destroy(t->entries[existing].tag);
        t->entries[existing].tag = child;
        return 0;
    }
    if (t->count == t->capacity) {
        size_t newCap = t->capacity == 0 ? 8 : t->capacity * 2;
        mcp_compound_entry_t* arr = (mcp_compound_entry_t*)realloc(t->entries,
            newCap * sizeof(mcp_compound_entry_t));
        if (!arr) return -1;
        t->entries = arr;
        t->capacity = newCap;
    }
    size_t nameLen = strlen(name);
    char* nameCopy = (char*)malloc(nameLen + 1);
    if (!nameCopy) return -1;
    memcpy(nameCopy, name, nameLen);
    nameCopy[nameLen] = '\0';
    t->entries[t->count].name     = nameCopy;
    t->entries[t->count].name_len = nameLen;
    t->entries[t->count].tag      = child;
    t->count++;
    return 0;
}

int mcp_compound_tag_set_byte  (mcp_tag_t* s, const char* n, int8_t v)   { return mcp_compound_tag_set(s, n, mcp_byte_tag_new_named(n, v)); }
int mcp_compound_tag_set_short (mcp_tag_t* s, const char* n, int16_t v)  { return mcp_compound_tag_set(s, n, mcp_short_tag_new_named(n, v)); }
int mcp_compound_tag_set_int   (mcp_tag_t* s, const char* n, int32_t v)  { return mcp_compound_tag_set(s, n, mcp_int_tag_new_named(n, v)); }
int mcp_compound_tag_set_long  (mcp_tag_t* s, const char* n, int64_t v)  { return mcp_compound_tag_set(s, n, mcp_long_tag_new_named(n, v)); }
int mcp_compound_tag_set_float (mcp_tag_t* s, const char* n, float v)    { return mcp_compound_tag_set(s, n, mcp_float_tag_new_named(n, v)); }
int mcp_compound_tag_set_double(mcp_tag_t* s, const char* n, double v)   { return mcp_compound_tag_set(s, n, mcp_double_tag_new_named(n, v)); }
int mcp_compound_tag_set_string(mcp_tag_t* s, const char* n, const char* v) {
    mcp_tag_t* t = mcp_string_tag_new_cstr(v);
    if (t) mcp_tag_set_name_cstr(t, n);
    return mcp_compound_tag_set(s, n, t);
}

const mcp_tag_t* mcp_compound_tag_get(const mcp_tag_t* self, const char* name) {
    if (!self || self->type != MCP_TAG_COMPOUND || !name) return NULL;
    const mcp_compound_tag_t* t = (const mcp_compound_tag_t*)self;
    int idx = find_entry(t, name);
    if (idx < 0) return NULL;
    return t->entries[idx].tag;
}

int8_t  mcp_compound_tag_get_byte  (const mcp_tag_t* s, const char* n, int8_t d)  {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_BYTE) return d;
    return ((const mcp_byte_tag_t*)t)->value;
}
int16_t mcp_compound_tag_get_short (const mcp_tag_t* s, const char* n, int16_t d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_SHORT) return d;
    return ((const mcp_short_tag_t*)t)->value;
}
int32_t mcp_compound_tag_get_int   (const mcp_tag_t* s, const char* n, int32_t d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_INT) return d;
    return ((const mcp_int_tag_t*)t)->value;
}
int64_t mcp_compound_tag_get_long  (const mcp_tag_t* s, const char* n, int64_t d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_LONG) return d;
    return ((const mcp_long_tag_t*)t)->value;
}
float   mcp_compound_tag_get_float (const mcp_tag_t* s, const char* n, float d)   {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_FLOAT) return d;
    return ((const mcp_float_tag_t*)t)->value;
}
double  mcp_compound_tag_get_double(const mcp_tag_t* s, const char* n, double d)  {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_DOUBLE) return d;
    return ((const mcp_double_tag_t*)t)->value;
}
const char* mcp_compound_tag_get_string(const mcp_tag_t* s, const char* n, const char* d) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_STRING) return d;
    const char* v = ((const mcp_string_tag_t*)t)->value;
    // This function MUST NOT return NULL. Almost every caller feeds the
    // result straight into a std::string, and std::string(NULL) is
    // undefined behaviour — in a release build it is strlen(NULL) and a
    // SIGSEGV. Returning the caller's default whenever the stored value is
    // missing makes the accessor total, so all 12 call sites are safe
    // rather than each having to guard its own result.
    return v ? v : (d ? d : "");
}

const mcp_tag_t* mcp_compound_tag_get_compound(const mcp_tag_t* s, const char* n) {
    const mcp_tag_t* t = mcp_compound_tag_get(s, n);
    if (!t || t->type != MCP_TAG_COMPOUND) return NULL;
    return t;
}

size_t mcp_compound_tag_size(const mcp_tag_t* self) {
    if (!self || self->type != MCP_TAG_COMPOUND) return 0;
    return ((const mcp_compound_tag_t*)self)->count;
}
bool mcp_compound_tag_contains(const mcp_tag_t* self, const char* name) {
    if (!self || self->type != MCP_TAG_COMPOUND || !name) return false;
    return find_entry((const mcp_compound_tag_t*)self, name) >= 0;
}

bool mcp_compound_tag_remove(mcp_tag_t* self, const char* name) {
    if (!self || self->type != MCP_TAG_COMPOUND || !name) return false;
    mcp_compound_tag_t* t = (mcp_compound_tag_t*)self;
    int idx = find_entry(t, name);
    if (idx < 0) return false;
    free(t->entries[idx].name);
    mcp_tag_destroy(t->entries[idx].tag);
    // Shift the tail down so the insertion order of the survivors is kept
    // (the writer emits entries in array order, so a hole would reorder the
    // serialized tag).
    for (size_t i = (size_t)idx + 1; i < t->count; ++i) {
        t->entries[i - 1] = t->entries[i];
    }
    t->count--;
    return true;
}
