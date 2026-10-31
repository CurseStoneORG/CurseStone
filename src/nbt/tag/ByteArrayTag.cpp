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
// ByteArrayTag.cpp — NBT TAG_Byte_Array payload.
#include "ByteArrayTag.h"

namespace cursestone::nbt {

const uint8_t* byte_array_data(const Tag* tag, size_t* out_len) {
    if (!tag || tag->type != TAG_BYTE_ARRAY)
        return nullptr;
    const auto* t = static_cast<const ByteArrayTag*>(tag);
    if (out_len)
        *out_len = t->data.size();
    // Keep the non-null promise for empty arrays.
    return t->data.empty() ? reinterpret_cast<const uint8_t*>("") : t->data.data();
}

void ByteArrayTag::write_payload(mcp_bstream_t* s, bool le) const {
    if (le)
        mcp_bstream_write_u32_le(s, (uint32_t)data.size());
    else
        mcp_bstream_write_u32_be(s, (uint32_t)data.size());
    if (!data.empty())
        mcp_bstream_write_bytes(s, data.data(), data.size());
}

} // namespace cursestone::nbt
