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
// EntityEventPacket — clientbound. Dispara animaciones/eventos
// cliente-side atados a una entidad (golpe recibido, muerte, etc).
// Wire format (protocol 84, mirrors the reference server EntityEventPacket):
//   [Int64 BE] eid
//   [Byte]     event
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class EntityEventPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return ENTITY_EVENT_PACKET;
    }

    // Events actually used by the current pipeline: combat animations
    // and the eating completion (event 9 — the client sends it when its
    // eat animation ends; the server answers with the same event).
    static constexpr uint8_t HURT_ANIMATION = 2;
    static constexpr uint8_t DEATH_ANIMATION = 3;
    static constexpr uint8_t TAME_FAIL = 6;
    static constexpr uint8_t TAME_SUCCESS = 7;
    static constexpr uint8_t SHAKE_WET = 8;
    static constexpr uint8_t USE_ITEM = 9; // finished eating
    static constexpr uint8_t EAT_GRASS_ANIMATION = 10;
    static constexpr uint8_t FISH_HOOK_BUBBLE = 11;
    static constexpr uint8_t FISH_HOOK_POSITION = 12;
    static constexpr uint8_t FISH_HOOK_HOOK = 13;
    static constexpr uint8_t FISH_HOOK_TEASE = 14;
    static constexpr uint8_t SQUID_INK_CLOUD = 15;
    static constexpr uint8_t AMBIENT_SOUND = 16;
    static constexpr uint8_t RESPAWN = 17;

    int64_t eid = 0;
    uint8_t event = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
