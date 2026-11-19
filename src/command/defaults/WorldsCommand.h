/*
 _____                     _____ _
/  __ \                   /  ___| |
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
// /worlds — the multiworld control surface for operators.
//   /worlds                 list every loaded world (dimension, generator,
//                           players, resident chunks)
//   /worlds tp <world>      switch the sender to a world's spawn
//   /worlds load <name> [generator] [seed]
//                           load (or create) a world
//   /worlds unload <name>   unload a world (players move to the default)
class WorldsCommand : public Command {
  public:
    WorldsCommand() : Command("worlds", "Manage the loaded worlds", "", "cursestone.command.worlds") {
    }
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
