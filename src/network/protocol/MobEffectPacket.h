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
// MobEffectPacket — adds, modifies, or removes a potion effect on an
// entity. Protocol 84 wire layout (big-endian, no varints):
//   [Long BE] eid
//   [Byte]    eventId (1 add, 2 modify, 3 remove)
//   [Byte]    effectId
//   [Byte]    amplifier
//   [Byte]    particles (1 visible, 0 hidden)
//   [Int BE]  duration in ticks
#pragma once

#include "DataPacket.h"
#include <cstdint>

namespace cursestone::network::protocol {

class MobEffectPacket : public DataPacket {
  public:
    static constexpr uint8_t EVENT_ADD = 1;
    static constexpr uint8_t EVENT_MODIFY = 2;
    static constexpr uint8_t EVENT_REMOVE = 3;

    uint8_t pid() const override {
        return MOB_EFFECT_PACKET;
    }

    uint64_t eid = 0;
    uint8_t eventId = EVENT_ADD;
    uint8_t effectId = 0;
    uint8_t amplifier = 0;
    bool particles = true;
    int32_t duration = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
