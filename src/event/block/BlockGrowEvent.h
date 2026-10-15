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
// BlockGrowEvent.h — ported from cn.nukkit.event.block.BlockGrowEvent
//
// Fired when a block grows (e.g. crops). Holds the coordinates of the
// growing block plus the coordinates of the new state that will replace it.
#pragma once
#include "BlockEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace cursestone::event::block {

class BlockGrowEvent : public BlockEvent, public Cancellable {
  public:
    int32_t newStateX, newStateY, newStateZ;
    BlockGrowEvent(int32_t bx, int32_t by, int32_t bz, int32_t nsx, int32_t nsy, int32_t nsz)
        : BlockEvent(bx, by, bz), newStateX(nsx), newStateY(nsy), newStateZ(nsz) {
    }

    const std::string& eventName() const override {
        static std::string n = "BlockGrowEvent";
        return n;
    }
};

} // namespace cursestone::event::block
