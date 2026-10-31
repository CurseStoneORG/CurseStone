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
// ByteTag — NBT TAG_BYTE value.
#pragma once

#include <cstdint>

#include "Tag.h"

namespace cursestone::nbt {

class ByteTag : public Tag {
  public:
    explicit ByteTag(int8_t v) : Tag(TAG_BYTE), value(v) {}
    ByteTag(const char* name, int8_t v) : Tag(TAG_BYTE), value(v) {
        if (name)
            this->name = name;
    }
    int8_t value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

} // namespace cursestone::nbt
