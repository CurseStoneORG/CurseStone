// SPDX-License-Identifier: LGPL-3.0-or-later
// ServerEvent.h — ported from cn.nukkit.event.server.ServerEvent
//
// Base class for all server-related events (network packets, query
// regenerations, console commands).
#pragma once
#include "../Event.h"
#include <string>
namespace atomikpe::event::server {

class ServerEvent : public Event {
public:
    const std::string& eventName() const override {
        static std::string n = "ServerEvent"; return n;
    }
};

} // namespace atomikpe::event::server
