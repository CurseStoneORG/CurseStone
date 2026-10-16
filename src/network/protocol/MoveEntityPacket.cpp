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
// MoveEntityPacket.cpp — encode/decode.
//
// The reference server: putLong (BE) + 3× putFloat (BE) + 3× putByte (scaled rotation).
// Rotation encoding: byte = angle_deg / (360.0 / 256) = angle_deg * 256.0 / 360.0.
#include "MoveEntityPacket.h"
#include <cmath>

namespace cursestone::network::protocol {

static inline uint8_t angleToByte(float deg) {
    // Normalize to [0, 360) then scale to [0, 256).
    if (!std::isfinite(deg))
        deg = 0.0f;
    deg = std::fmod(deg, 360.0f);
    if (deg < 0.0f)
        deg += 360.0f;
    float b = deg * (256.0f / 360.0f);
    return static_cast<uint8_t>(b);
}
static inline float byteToAngle(uint8_t b) {
    return static_cast<float>(b) * (360.0f / 256.0f);
}

void MoveEntityPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    w.writeF32BE(x);
    w.writeF32BE(y);
    w.writeF32BE(z);
    w.writeU8(angleToByte(pitch));
    w.writeU8(angleToByte(yaw));
    w.writeU8(angleToByte(headYaw));
}

bool MoveEntityPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    uint8_t bPitch, bYaw, bHeadYaw;
    if (!r.readI64BE(eid))
        return false;
    if (!r.readF32BE(x))
        return false;
    if (!r.readF32BE(y))
        return false;
    if (!r.readF32BE(z))
        return false;
    if (!r.readU8(bPitch))
        return false;
    if (!r.readU8(bYaw))
        return false;
    if (!r.readU8(bHeadYaw))
        return false;
    pitch = byteToAngle(bPitch);
    yaw = byteToAngle(bYaw);
    headYaw = byteToAngle(bHeadYaw);
    return true;
}

} // namespace cursestone::network::protocol
