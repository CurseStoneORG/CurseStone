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
// StringTag.cpp — NBT TAG_String payload.
#include "StringTag.h"

namespace cursestone::nbt {

void StringTag::write_payload(mcp_bstream_t* s, bool le) const {
    if (le) {
        // MCPE strings use uint16 little-endian prefix
        mcp_bstream_write_u16_le(s, (uint16_t)value.size());
    } else {
        // Java NBT strings use uint16 big-endian prefix
        mcp_bstream_write_u16_be(s, (uint16_t)value.size());
    }
    if (!value.empty())
        mcp_bstream_write_bytes(s, value.data(), value.size());
}

} // namespace cursestone::nbt
