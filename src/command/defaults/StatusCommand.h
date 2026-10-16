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
// StatusCommand.h — /status: RAM, TPS, uptime, players, plugins and per-world tick time.
#pragma once
#include "command/Command.h"
#include <string>
#include <vector>
namespace cursestone {
/**
 * @brief Prints a server health summary. Restricted to operators and the console.
 *
 * Data is collected on the main thread (scheduled), because the console runs commands
 * on the CommandReader thread and world/player state must not be read from there.
 */
class StatusCommand : public Command {
  public:
    StatusCommand()
        : Command("status", "Show server RAM, TPS, players, plugins and world tick time", "",
                  "cursestone.command.status") {
    }
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace cursestone
