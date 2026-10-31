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
// StringTag — NBT TAG_String (uint16 length prefix + UTF-8 bytes).
#pragma once

#include <cstddef>
#include <string>

#include "Tag.h"

namespace cursestone::nbt {

class StringTag : public Tag {
  public:
    StringTag(const char* value, size_t len) : Tag(TAG_STRING) {
        if (len)
            this->value.assign(value, len);
    }
    explicit StringTag(const char* value) : Tag(TAG_STRING) {
        if (value)
            this->value = value;
    }
    StringTag(const char* name, const char* value) : Tag(TAG_STRING) {
        if (name)
            this->name = name;
        if (value)
            this->value = value;
    }
    std::string value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

} // namespace cursestone::nbt
