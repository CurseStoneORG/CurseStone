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
// PlayerMessageEvent.h — ported from cn.nukkit.event.player.PlayerMessageEvent
//
// Base class for player chat / command events. Not cancellable by
// itself — subclasses opt into Cancellable as needed.
#pragma once
#include "PlayerEvent.h"
#include <string>
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerMessageEvent : public PlayerEvent {
public:
    std::string message;
    PlayerMessageEvent(cursestone::Player* p, const std::string& msg)
        : PlayerEvent(p), message(msg) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerMessageEvent"; return n;
    }
};
} // namespace cursestone::event::player
