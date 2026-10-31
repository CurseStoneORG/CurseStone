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
// IntArrayTag — NBT TAG_Int_Array (uint32 count prefix + int32s).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Tag.h"

namespace cursestone::nbt {

class IntArrayTag : public Tag {
  public:
    IntArrayTag() : Tag(TAG_INT_ARRAY) {}
    IntArrayTag(const int32_t* data, size_t count) : Tag(TAG_INT_ARRAY) {
        if (count)
            this->data.assign(data, data + count);
    }
    IntArrayTag(const char* name, const int32_t* data, size_t count) : Tag(TAG_INT_ARRAY) {
        if (name)
            this->name = name;
        if (count)
            this->data.assign(data, data + count);
    }
    std::vector<int32_t> data;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

// Payload ints of `tag`, storing their count in *out_count. Returns
// nullptr when the tag is missing or not an int array. A valid empty
// array yields a non-null pointer and *out_count == 0.
const int32_t* int_array_data(const Tag* tag, size_t* out_count);

} // namespace cursestone::nbt
