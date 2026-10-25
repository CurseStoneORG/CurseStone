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
// ByteArrayTag.cpp — NBT TAG_Byte_Array.
#include "ByteArrayTag.h"
#include "TagDefs.h"

mcp_tag_t* mcp_byte_array_tag_new(const uint8_t* data, size_t len) {
    try {
        auto* t = new mcp_byte_array_tag();
        if (len)
            t->data.assign(data, data + len);
        return t;
    } catch (...) {
        return NULL;
    }
}

mcp_tag_t* mcp_byte_array_tag_new_named(const char* name, const uint8_t* data, size_t len) {
    mcp_tag_t* t = mcp_byte_array_tag_new(data, len);
    if (t)
        mcp_tag_set_name_cstr(t, name);
    return t;
}

const uint8_t* mcp_byte_array_tag_data(const mcp_tag_t* tag, size_t* out_len) {
    if (!tag || tag->type != MCP_TAG_BYTE_ARRAY)
        return NULL;
    const auto* t = static_cast<const mcp_byte_array_tag*>(tag);
    if (out_len)
        *out_len = t->data.size();
    // Keep the non-NULL promise for empty arrays.
    return t->data.empty() ? reinterpret_cast<const uint8_t*>("") : t->data.data();
}

void mcp_byte_array_tag::write_payload(mcp_bstream_t* s, bool le) const {
    if (le)
        mcp_bstream_write_u32_le(s, (uint32_t)data.size());
    else
        mcp_bstream_write_u32_be(s, (uint32_t)data.size());
    if (!data.empty())
        mcp_bstream_write_bytes(s, data.data(), data.size());
}
