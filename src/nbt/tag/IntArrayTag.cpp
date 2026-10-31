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
// IntArrayTag.cpp — NBT TAG_Int_Array payload.
#include "IntArrayTag.h"

namespace cursestone::nbt {

const int32_t* int_array_data(const Tag* tag, size_t* out_count) {
    if (!tag || tag->type != TAG_INT_ARRAY)
        return nullptr;
    const auto* t = static_cast<const IntArrayTag*>(tag);
    if (out_count)
        *out_count = t->data.size();
    // Keep the non-null promise for empty arrays.
    return t->data.empty() ? reinterpret_cast<const int32_t*>("") : t->data.data();
}

void IntArrayTag::write_payload(mcp_bstream_t* s, bool le) const {
    if (le)
        mcp_bstream_write_u32_le(s, (uint32_t)data.size());
    else
        mcp_bstream_write_u32_be(s, (uint32_t)data.size());
    for (int32_t v : data) {
        if (le)
            mcp_bstream_write_u32_le(s, (uint32_t)v);
        else
            mcp_bstream_write_u32_be(s, (uint32_t)v);
    }
}

} // namespace cursestone::nbt
