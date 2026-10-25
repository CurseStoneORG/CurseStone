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
// DoubleTag.cpp — NBT MCP_TAG_DOUBLE value.
#include "DoubleTag.h"
#include "TagDefs.h"

mcp_tag_t* mcp_double_tag_new(double v) {
    try {
        return new mcp_double_tag(v);
    } catch (...) {
        return NULL;
    }
}

mcp_tag_t* mcp_double_tag_new_named(const char* name, double v) {
    try {
        auto* t = new mcp_double_tag(v);
        if (name)
            t->name = name;
        return t;
    } catch (...) {
        return NULL;
    }
}

void mcp_double_tag::write_payload(mcp_bstream_t* s, bool le) const {
    (void)le;
    if (le)
        mcp_bstream_write_f64_le(s, value);
    else
        mcp_bstream_write_f64_be(s, value);
}
