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
// ByteArrayTag — NBT TAG_Byte_Array (uint32 length prefix + bytes).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Tag.h"

namespace cursestone::nbt {

class ByteArrayTag : public Tag {
  public:
    ByteArrayTag() : Tag(TAG_BYTE_ARRAY) {}
    ByteArrayTag(const uint8_t* data, size_t len) : Tag(TAG_BYTE_ARRAY) {
        if (len)
            this->data.assign(data, data + len);
    }
    ByteArrayTag(const char* name, const uint8_t* data, size_t len) : Tag(TAG_BYTE_ARRAY) {
        if (name)
            this->name = name;
        if (len)
            this->data.assign(data, data + len);
    }
    std::vector<uint8_t> data;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

// Payload bytes of `tag`, storing their count in *out_len. Returns
// nullptr when the tag is missing or not a byte array. A valid empty
// array yields a non-null pointer and *out_len == 0.
const uint8_t* byte_array_data(const Tag* tag, size_t* out_len);

} // namespace cursestone::nbt
