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
// ByteTag.cpp — NBT MCP_TAG_BYTE value.
#include "ByteTag.h"
#include "TagDefs.h"

mcp_tag_t* mcp_byte_tag_new(int8_t v) {
    try {
        return new mcp_byte_tag(v);
    } catch (...) {
        return NULL;
    }
}

mcp_tag_t* mcp_byte_tag_new_named(const char* name, int8_t v) {
    try {
        auto* t = new mcp_byte_tag(v);
        if (name)
            t->name = name;
        return t;
    } catch (...) {
        return NULL;
    }
}

void mcp_byte_tag::write_payload(mcp_bstream_t* s, bool le) const {
    (void)le;
    mcp_bstream_write_i8(s, value);
}
