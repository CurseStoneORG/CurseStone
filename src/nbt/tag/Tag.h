// SPDX-License-Identifier: LGPL-3.0-or-later
// Tag — base NBT tag interface. Pure C (typedef struct + vtable).
//
// NBT (Named Binary Tag) is Minecraft's serialization format. Used for:
//   - LevelDB chunk data (MCPE-format chunks)
//   - Player inventory / player .dat files
//   - Block entity (tile) data: signs, chests, furnaces
//   - Item NBT data (enchantments, custom names, durability)
//
// Endianness:
//   - Java/NBT big-endian (used by the Nukkit Anvil provider for
//     chunks, level.dat, and player .dat files)
//   - MCPE little-endian (used by leveldb chunks, network packets)
//
// All tag types share a common header (struct mcp_tag) with a type id,
// a name, and a vtable for serialize/deserialize/destroy. Subtypes
// embed the header as their first member so they can be cast safely.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mcp_tag_type {
    MCP_TAG_END       = 0,
    MCP_TAG_BYTE      = 1,
    MCP_TAG_SHORT     = 2,
    MCP_TAG_INT       = 3,
    MCP_TAG_LONG      = 4,
    MCP_TAG_FLOAT     = 5,
    MCP_TAG_DOUBLE    = 6,
    MCP_TAG_BYTE_ARRAY= 7,
    MCP_TAG_STRING    = 8,
    MCP_TAG_LIST      = 9,
    MCP_TAG_COMPOUND  = 10,
    MCP_TAG_INT_ARRAY= 11,
} mcp_tag_type_t;

// Forward declaration of the public Tag interface.
typedef struct mcp_tag mcp_tag_t;

// Vtable: polymorphic operations on any tag.
struct mcp_tag_vtable {
    void (*destroy)(mcp_tag_t* self);
    // Serialize this tag's payload (NOT including the leading type byte
    // or name) into a BinaryStream.
    void (*write_payload)(const mcp_tag_t* self, void* stream, bool little_endian);
    // Pretty-print to a string builder (debugging).
    void (*to_string)(const mcp_tag_t* self, char* out, size_t out_size);
};

// Base tag header. All concrete tags embed this as the first member.
struct mcp_tag {
    const struct mcp_tag_vtable* vtable;
    mcp_tag_type_t type;
    char* name;          // NUL-terminated; may be NULL for list elements
    size_t name_len;
};

// Generic helper: free a tag (dispatches via vtable).
void mcp_tag_destroy(mcp_tag_t* tag);

// Get the type of a tag.
mcp_tag_type_t mcp_tag_get_type(const mcp_tag_t* tag);

// Set the name. Copies the string (caller keeps ownership of `name`).
void mcp_tag_set_name(mcp_tag_t* tag, const char* name, size_t len);
void mcp_tag_set_name_cstr(mcp_tag_t* tag, const char* name);

const char* mcp_tag_type_name(mcp_tag_type_t t);

#ifdef __cplusplus
} // extern "C"
#endif
