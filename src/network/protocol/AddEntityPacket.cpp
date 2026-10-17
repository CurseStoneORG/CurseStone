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
// AddEntityPacket.cpp — encoder per the reference server AddEntityPacket.
#include "AddEntityPacket.h"

namespace cursestone::network::protocol {

void AddEntityPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeI32BE(type);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeF32BE(speedX);
    w.writeF32BE(speedY);
    w.writeF32BE(speedZ);
    // Reference encoder: yaw written as float(yaw * 0.71111) — the 0.15.x
    // client expects the packed 0..255 rotation representation.
    constexpr float kRotScale = 0.71111f;
    w.writeF32BE(yawPitchScaled ? yaw * kRotScale : yaw);
    w.writeF32BE(yawPitchScaled ? pitch * kRotScale : pitch);
    w.writeI32BE(modifiers);
    if (metadata.empty()) {
        // Empty metadata: the reference server Binary::writeMetadata always
        // appends the 0x7f terminator, even with zero entries. A plain 0
        // byte parses as a BYTE/FLAGS entry and the client then consumes
        // the trailing link bytes as garbage (this glitched the arrow
        // spawn on other clients).
        w.writeU8(SetEntityDataPacket::TERMINATOR);
    } else {
        // The reference server Binary::writeMetadata: key byte (type << 5) | index,
        // then the value — INT is LITTLE-endian (writeLInt) on protocol
        // 84 — then the 0x7F terminator.
        for (const SetEntityDataPacket::Entry& e : metadata) {
            w.writeU8((uint8_t)(((e.type & 0x07) << 5) | (e.index & 0x1F)));
            switch (e.type) {
            case SetEntityDataPacket::TYPE_BYTE:
                w.writeU8((uint8_t)(e.ival & 0xFF));
                break;
            case SetEntityDataPacket::TYPE_SHORT:
                w.writeU16LE((uint16_t)(e.ival & 0xFFFF));
                break;
            case SetEntityDataPacket::TYPE_INT:
                w.writeU32LE((uint32_t)e.ival);
                break;
            case SetEntityDataPacket::TYPE_LONG:
                // Binary::writeLLong — little-endian like every other
                // metadata value.
                w.writeU64LE((uint64_t)e.ival);
                break;
            case SetEntityDataPacket::TYPE_FLOAT:
                // Binary::writeLFloat.
                w.writeF32LE(e.fval);
                break;
            case SetEntityDataPacket::TYPE_STRING:
                // Binary::writeMetadata string: little-endian length.
                w.writeU16LE((uint16_t)e.sval.size());
                w.writeBytes(reinterpret_cast<const uint8_t*>(e.sval.data()), e.sval.size());
                break;
            case SetEntityDataPacket::TYPE_POS:
                w.writeU32LE((uint32_t)e.px);
                w.writeU32LE((uint32_t)e.py);
                w.writeU32LE((uint32_t)e.pz);
                break;
            default:
                break;
            }
        }
        w.writeU8(SetEntityDataPacket::TERMINATOR);
    }
    // No entity links.
    w.writeI16BE(0);
}

bool AddEntityPacket::decode(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return true; // server→client only
}

} // namespace cursestone::network::protocol
