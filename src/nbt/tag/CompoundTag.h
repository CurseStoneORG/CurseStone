// SPDX-License-Identifier: LGPL-3.0-or-later
// CompoundTag — unordered map of name→tag. The most common NBT
// container: every chunk's "Level" root, every block entity, every
// player.dat file is a CompoundTag.
#pragma once

#include "Tag.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_compound_entry {
    char*      name;       // owned UTF-8 buffer, NUL-terminated
    size_t     name_len;
    mcp_tag_t* tag;        // owned
} mcp_compound_entry_t;

typedef struct mcp_compound_tag {
    mcp_tag_t header;
    mcp_compound_entry_t* entries;
    size_t    count;
    size_t    capacity;
} mcp_compound_tag_t;

mcp_tag_t* mcp_compound_tag_new(void);
mcp_tag_t* mcp_compound_tag_new_named(const char* name);

// Insert or replace a child. The compound takes ownership of `child`.
// Returns 0 on success.
int mcp_compound_tag_set(mcp_tag_t* self, const char* name, mcp_tag_t* child);

// Convenience wrappers for primitives.
int mcp_compound_tag_set_byte(mcp_tag_t* self, const char* name, int8_t v);
int mcp_compound_tag_set_short(mcp_tag_t* self, const char* name, int16_t v);
int mcp_compound_tag_set_int(mcp_tag_t* self, const char* name, int32_t v);
int mcp_compound_tag_set_long(mcp_tag_t* self, const char* name, int64_t v);
int mcp_compound_tag_set_float(mcp_tag_t* self, const char* name, float v);
int mcp_compound_tag_set_double(mcp_tag_t* self, const char* name, double v);
int mcp_compound_tag_set_string(mcp_tag_t* self, const char* name, const char* v);

// Lookup. Returns NULL if missing.
const mcp_tag_t* mcp_compound_tag_get(const mcp_tag_t* self, const char* name);

// Convenience getters (NULL/0 if missing or wrong type).
int8_t   mcp_compound_tag_get_byte(const mcp_tag_t* self, const char* name, int8_t def);
int16_t  mcp_compound_tag_get_short(const mcp_tag_t* self, const char* name, int16_t def);
int32_t  mcp_compound_tag_get_int(const mcp_tag_t* self, const char* name, int32_t def);
int64_t  mcp_compound_tag_get_long(const mcp_tag_t* self, const char* name, int64_t def);
float    mcp_compound_tag_get_float(const mcp_tag_t* self, const char* name, float def);
double   mcp_compound_tag_get_double(const mcp_tag_t* self, const char* name, double def);
const char* mcp_compound_tag_get_string(const mcp_tag_t* self, const char* name, const char* def);

// Returns the named child as a CompoundTag (NULL if not a compound).
const mcp_tag_t* mcp_compound_tag_get_compound(const mcp_tag_t* self, const char* name);

size_t mcp_compound_tag_size(const mcp_tag_t* self);
bool mcp_compound_tag_contains(const mcp_tag_t* self, const char* name);

#ifdef __cplusplus
} // extern "C"
#endif
