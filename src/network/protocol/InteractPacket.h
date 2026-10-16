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
// InteractPacket — serverbound. El cliente lo envia al hacer click
// (izq/der) sobre una entidad. Wire format (protocol 84, mirrors
// The reference server legacy InteractPacket):
//   [Byte]     action
//   [Int64 BE] target (runtime eid de la entidad clickeada)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class InteractPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return INTERACT_PACKET;
    }

    static constexpr uint8_t ACTION_RIGHT_CLICK = 1;
    static constexpr uint8_t ACTION_LEFT_CLICK = 2;
    static constexpr uint8_t ACTION_LEAVE_VEHICLE = 3;

    uint8_t action = 0;
    int64_t target = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
