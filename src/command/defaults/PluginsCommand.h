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
// PluginsCommand — /plugins: lista los plugins Lua cargados con su
// version and state. Follows the same pattern as /version or /list.
//
// OPs and console only: SimpleCommandMap::dispatch() checks the
// "cursestone.command.plugins" node BEFORE execute(), so a non-op player
// never reaches the list (and /help hides it through canUse()). Ops
// cover the node via the "cursestone.*" subtree Player::hasPermission()
// grants them by default. /plugins reload additionally keeps its own
// isOp() check.
#pragma once
#include "command/Command.h"

namespace cursestone {

class PluginsCommand : public Command {
  public:
    PluginsCommand()
        : Command("plugins", "List loaded plugins (op only)", "/plugins [reload]",
                  "cursestone.command.plugins") {
    }
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};

}
