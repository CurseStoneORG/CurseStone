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
// SetEntityMotionPacket — server→client entity velocity (knockback).
//
// Wire format (protocol 84) — Genisys 0.15.0
// SetEntityMotionPacket::encode:
//
//     for each entity:
//         [Int64 BE] eid
//         [Float BE] motionX
//         [Float BE] motionY
//         [Float BE] motionZ
//
// No count field, no terminator — just the flat run of records. The reference server
// accumulates per-entity motion into this packet each tick
// (World::tick() -> the per-tick motion queue), which is how knockback
// (Living::knockBack() -> setMotion()) reaches other clients: without
// it a hit only ever moved the victim's own client and everyone else
// watched them stand still.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class SetEntityMotionPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return SET_ENTITY_MOTION_PACKET;
    }

    struct Motion {
        int64_t eid = 0;
        float motX = 0.0f;
        float motY = 0.0f;
        float motZ = 0.0f;
    };

    std::vector<Motion> entities;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol