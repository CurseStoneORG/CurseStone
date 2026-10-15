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
// ServerCommandEvent — fired when the console issues a command.
#pragma once
#include "event/Event.h"
#include "event/Cancellable.h"
#include <string>
namespace cursestone {
class CommandSender;
}
namespace cursestone::event::server {
class ServerCommandEvent : public Event, public Cancellable {
  public:
    cursestone::CommandSender* sender;
    std::string command;
    ServerCommandEvent(cursestone::CommandSender* s, std::string cmd)
        : sender(s), command(std::move(cmd)) {
    }
    const std::string& eventName() const override {
        static std::string n = "ServerCommandEvent";
        return n;
    }
};
} // namespace
