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
// ItemFrameDropItemPacket — cliente→servidor. El jugador rompe/golpea un
// item frame para soltar el item que cuelga de él.
//
// Wire format (protocolo 84, mirrors Genisys ItemFrameDropItemPacket):
//   [Int BE] z
//   [Int BE] y
//   [Int BE] x
//   [Slot]   dropItem      // item que el cliente tenía en el frame
// (el orden z,y,x es el que usa Genisys, no x,y,z)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class ItemFrameDropItemPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return ITEM_FRAME_DROP_ITEM_PACKET;
    }

    int32_t x = 0, y = 0, z = 0;
    int16_t itemId = 0;
    uint8_t itemCount = 0;
    int16_t itemDamage = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
