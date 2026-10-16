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
// CommandSender — anything that can issue commands (console,
// players, RCON clients). Mirrors command/CommandSender.
#pragma once
#include <string>
#include <memory>
namespace cursestone {
class CommandSender {
  public:
    virtual ~CommandSender() = default;
    // Where to send messages back to.
    virtual void sendMessage(const std::string& msg) = 0;
    // Identity: "CONSOLE" or player username.
    virtual std::string getName() const = 0;
    // Whether the sender has the given permission node.
    virtual bool hasPermission(const std::string& perm) const = 0;
    // Whether the sender is the server console.
    virtual bool isOp() const = 0;
};
} // namespace
