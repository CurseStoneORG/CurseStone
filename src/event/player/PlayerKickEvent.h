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
// PlayerKickEvent.h — ported from cn.nukkit.event.player.PlayerKickEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerKickEvent : public PlayerEvent, public Cancellable {
public:
    std::string reason;
    PlayerKickEvent(cursestone::Player* p, const std::string& r)
        : PlayerEvent(p), reason(r) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerKickEvent"; return n;
    }
};
} // namespace cursestone::event::player
