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
// PlayerToggleSprintEvent.h — ported from cn.nukkit.event.player.PlayerToggleSprintEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerToggleSprintEvent : public PlayerEvent, public Cancellable {
public:
    bool isSprinting = false;
    PlayerToggleSprintEvent(cursestone::Player* p, bool sprinting)
        : PlayerEvent(p), isSprinting(sprinting) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerToggleSprintEvent"; return n;
    }
};
} // namespace cursestone::event::player
