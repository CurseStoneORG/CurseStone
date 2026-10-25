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
// TagDefs — the real C++ tag classes behind the opaque mcp_tag_t API.
// C++ only; consumers use the mcp_* free functions declared in the
// public tag headers and never name these types directly (tools/nbt_test
// is the one deliberate white-box user).
#pragma once

#include "Tag.h"
#include "../../utils/BinaryStream.h"

#include <string>
#include <vector>

struct mcp_tag {
    explicit mcp_tag(mcp_tag_type_t t) : type(t) {}
    virtual ~mcp_tag() = default;
    mcp_tag(const mcp_tag&) = delete;
    mcp_tag& operator=(const mcp_tag&) = delete;

    mcp_tag_type_t type;
    std::string name; // empty = unnamed (list elements)

    // Serialize this tag's payload (NOT the leading type byte or name).
    virtual void write_payload(mcp_bstream_t* s, bool le) const { (void)s; (void)le; }
};

struct mcp_end_tag : mcp_tag {
    mcp_end_tag() : mcp_tag(MCP_TAG_END) {}
};

struct mcp_byte_tag : mcp_tag {
    explicit mcp_byte_tag(int8_t v) : mcp_tag(MCP_TAG_BYTE), value(v) {}
    int8_t value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_short_tag : mcp_tag {
    explicit mcp_short_tag(int16_t v) : mcp_tag(MCP_TAG_SHORT), value(v) {}
    int16_t value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_int_tag : mcp_tag {
    explicit mcp_int_tag(int32_t v) : mcp_tag(MCP_TAG_INT), value(v) {}
    int32_t value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_long_tag : mcp_tag {
    explicit mcp_long_tag(int64_t v) : mcp_tag(MCP_TAG_LONG), value(v) {}
    int64_t value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_float_tag : mcp_tag {
    explicit mcp_float_tag(float v) : mcp_tag(MCP_TAG_FLOAT), value(v) {}
    float value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_double_tag : mcp_tag {
    explicit mcp_double_tag(double v) : mcp_tag(MCP_TAG_DOUBLE), value(v) {}
    double value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_string_tag : mcp_tag {
    mcp_string_tag() : mcp_tag(MCP_TAG_STRING) {}
    std::string value;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_byte_array_tag : mcp_tag {
    mcp_byte_array_tag() : mcp_tag(MCP_TAG_BYTE_ARRAY) {}
    std::vector<uint8_t> data;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_int_array_tag : mcp_tag {
    mcp_int_array_tag() : mcp_tag(MCP_TAG_INT_ARRAY) {}
    std::vector<int32_t> data;
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_list_tag : mcp_tag {
    explicit mcp_list_tag(mcp_tag_type_t et) : mcp_tag(MCP_TAG_LIST), element_type(et) {}
    ~mcp_list_tag() override;
    mcp_tag_type_t element_type;
    std::vector<mcp_tag_t*> children; // owned, destroyed together with the list
    void write_payload(mcp_bstream_t* s, bool le) const override;
};

struct mcp_compound_entry {
    mcp_compound_entry(std::string n, mcp_tag_t* t) : name(std::move(n)), tag(t) {}
    std::string name;
    mcp_tag_t* tag; // owned
};

struct mcp_compound_tag : mcp_tag {
    mcp_compound_tag() : mcp_tag(MCP_TAG_COMPOUND) {}
    ~mcp_compound_tag() override;
    std::vector<mcp_compound_entry> entries; // insertion order preserved (wire format)
    void write_payload(mcp_bstream_t* s, bool le) const override;
};
