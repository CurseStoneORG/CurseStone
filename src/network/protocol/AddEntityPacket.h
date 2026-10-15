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
// AddEntityPacket.h — spawn a non-player entity on the client.
//
// Wire format (protocol 84, verified against Dr1xyDev/Infinity
// AddEntityPacket.php — the MCPE 0.15.10 reference):
//   [Long  BE] eid
//   [Int   BE] type        (entity network id, e.g. 93 = Lightning)
//   [Float BE] x, y, z
//   [Float BE] speedX, speedY, speedZ
//   [Float BE] yaw         (Infinity writes yaw * 0.71111)
//   [Float BE] pitch       (Infinity writes pitch * 0.71111)
//   [Int   BE] modifiers
//   [metadata blob]        (empty = single 0 byte)
//   [Short BE] links count + links (we never send links)
#pragma once

#include "DataPacket.h"
#include "SetEntityDataPacket.h"   // metadata Entry (same wire encoding)
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class AddEntityPacket : public DataPacket {
public:
    uint8_t pid() const override { return ADD_ENTITY_PACKET; }

    // Entity type network ids (protocol 84 / Infinity).
    static constexpr int32_t TYPE_LIGHTNING = 93;
    static constexpr int32_t TYPE_ARROW     = 80;   // Infinity Arrow::NETWORK_ID
    static constexpr int32_t TYPE_FALLING_BLOCK = 66;  // Infinity FallingSand::NETWORK_ID

    int64_t eid = 0;
    int32_t type = 0;
    float x = 0, y = 0, z = 0;
    float speedX = 0, speedY = 0, speedZ = 0;
    float yaw = 0, pitch = 0;
    int32_t modifiers = 0;
    bool yawPitchScaled = true;   // Infinity multiplies both by 0.71111
    // Optional entity metadata (Infinity: `$pk->metadata =
    // $this->dataProperties`, Binary::writeMetadata wire format: one
    // key byte `(type << 5) | index`, the value, then the 0x7F
    // terminator). Falling blocks ship DATA_BLOCK_INFO (20) here so the
    // 0.15.10 client knows which block to render. EMPTY keeps the
    // historical single-0x00 body this server always sent (arrows,
    // lightning and players are spawned with it and render fine).
    std::vector<SetEntityDataPacket::Entry> metadata;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
