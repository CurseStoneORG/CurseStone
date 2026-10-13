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
// QueryRegenerateEvent.h — ported from cn.nukkit.event.server.QueryRegenerateEvent
//
// Fired when the server query (used by the legacy Minecraft query
// protocol) is regenerated. Handlers can tweak the exposed server
// info (name, player counts, map, etc.).
#pragma once
#include "ServerEvent.h"
#include <string>

namespace cursestone::event::server {

class QueryRegenerateEvent : public ServerEvent {
public:
    int timeout = 0;
    std::string serverName;
    int numPlayers = 0;
    int maxPlayers = 0;
    std::string map;
    std::string ip;
    int port = 0;

    QueryRegenerateEvent() = default;

    const std::string& eventName() const override {
        static std::string n = "QueryRegenerateEvent"; return n;
    }
};

} // namespace cursestone::event::server
