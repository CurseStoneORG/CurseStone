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
// BlockSpreadEvent.h — ported from cn.nukkit.event.block.BlockSpreadEvent
//
// Fired when a block spreads to another block (e.g. grass spreading to
// dirt, mushrooms spreading). Holds both the source coordinates and the
// new state coordinates.
#pragma once
#include "BlockFormEvent.h"
#include <cstdint>
namespace cursestone::event::block {

// NOTA: no hereda Cancellable directamente — llega vía BlockFormEvent
// -> BlockGrowEvent. Repetirlo aquí crearía un tercer subobjeto
// Cancellable en el mismo objeto (diamante no-virtual), ambiguo en
// tiempo de compilación para isCancelled()/setCancelled().
class BlockSpreadEvent : public BlockFormEvent {
  public:
    int32_t sourceX, sourceY, sourceZ;
    BlockSpreadEvent(int32_t bx, int32_t by, int32_t bz, int32_t sx, int32_t sy, int32_t sz,
                     int32_t nsx, int32_t nsy, int32_t nsz)
        : BlockFormEvent(bx, by, bz, nsx, nsy, nsz), sourceX(sx), sourceY(sy), sourceZ(sz) {
    }

    const std::string& eventName() const override {
        static std::string n = "BlockSpreadEvent";
        return n;
    }
};

} // namespace cursestone::event::block
