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
