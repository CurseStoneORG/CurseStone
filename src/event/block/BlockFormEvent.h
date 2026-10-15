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
// BlockFormEvent.h — ported from cn.nukkit.event.block.BlockFormEvent
//
// Fired when a block is formed in the world (e.g. snow forming, ice
// freezing, concrete powder solidifying). Uses the parent's newState
// coordinates to describe the new state.
#pragma once
#include "BlockGrowEvent.h"
#include <cstdint>
namespace cursestone::event::block {

// NOTA: no hereda Cancellable directamente — BlockGrowEvent ya lo hace.
// Heredarlo de nuevo aquí crearía un diamante no-virtual (dos
// subobjetos Cancellable), lo que vuelve ambiguos isCancelled()/
// setCancelled() en tiempo de compilación.
class BlockFormEvent : public BlockGrowEvent {
  public:
    BlockFormEvent(int32_t bx, int32_t by, int32_t bz, int32_t nsx, int32_t nsy, int32_t nsz)
        : BlockGrowEvent(bx, by, bz, nsx, nsy, nsz) {
    }

    const std::string& eventName() const override {
        static std::string n = "BlockFormEvent";
        return n;
    }
};

} // namespace cursestone::event::block
