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
// TextPacket.cpp — encode/decode.
//
// PMMP putByte / putString (= BE short length + bytes).
#include "TextPacket.h"

#include <cstdint>

namespace cursestone::network::protocol {

void TextPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(type);
    switch (type) {
        case TYPE_POPUP:
        case TYPE_CHAT:
            w.writeBEString(source);
            // fall through — write message next
            [[fallthrough]];
        case TYPE_RAW:
        case TYPE_TIP:
        case TYPE_SYSTEM:
            w.writeBEString(message);
            break;
        case TYPE_TRANSLATION:
            w.writeBEString(message);
            w.writeU8(static_cast<uint8_t>(parameters.size()));
            for (const auto& p : parameters) w.writeBEString(p);
            break;
        default:
            // Unknown type — emit raw message to keep wire shape consistent.
            w.writeBEString(message);
            break;
    }
}

bool TextPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU8(type)) return false;
    switch (type) {
        case TYPE_POPUP:
        case TYPE_CHAT:
            if (!r.readBEString(source)) return false;
            // fall through
            [[fallthrough]];
        case TYPE_RAW:
        case TYPE_TIP:
        case TYPE_SYSTEM:
            return r.readBEString(message);
        case TYPE_TRANSLATION:
            if (!r.readBEString(message)) return false;
            uint8_t count;
            if (!r.readU8(count)) return false;
            parameters.clear();
            parameters.reserve(count);
            for (uint8_t i = 0; i < count; ++i) {
                std::string p;
                if (!r.readBEString(p)) return false;
                parameters.push_back(std::move(p));
            }
            return true;
    }
    return false;
}

} // namespace cursestone::network::protocol
