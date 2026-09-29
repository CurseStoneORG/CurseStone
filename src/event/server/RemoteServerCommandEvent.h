// SPDX-License-Identifier: LGPL-3.0-or-later
// RemoteServerCommandEvent.h — ported from cn.nukkit.event.server.RemoteServerCommandEvent
//
// Fired when a remote console sender (e.g. RCON) issues a command.
// Inherits the sender/command fields from ServerCommandEvent.
#pragma once
#include "ServerCommandEvent.h"
#include <string>
namespace cursestone { class CommandSender; }

namespace cursestone::event::server {

class RemoteServerCommandEvent : public ServerCommandEvent {
public:
    RemoteServerCommandEvent(void* sender, const std::string& cmd)
        : ServerCommandEvent(static_cast<cursestone::CommandSender*>(sender), cmd) {}

    const std::string& eventName() const override {
        static std::string n = "RemoteServerCommandEvent"; return n;
    }
};

} // namespace cursestone::event::server
