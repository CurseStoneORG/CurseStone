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
#include "HelpCommand.h"
#include "Server.h"
#include "command/CommandMap.h"
#include "command/Command.h"
#include <algorithm>
#include <vector>

namespace cursestone {

// /help — DYNAMIC command list built from the real CommandMap
// (SimpleCommandMap::allCommands()), NOT a fixed table, so plugin
// commands show up on their own and only if the sender can use them
// (Command::canUse). Aliases are commands too and show as such.
bool HelpCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>& args) {
    // Parse page number from args
    int pageNumber = 1;
    if (!args.empty()) {
        try {
            pageNumber = std::stoi(args.back());
            if (pageNumber < 1)
                pageNumber = 1;
        } catch (...) {}
    }

    // Collect the commands THIS sender can use, with description.
    struct CmdEntry {
        std::string name;
        std::string description;
    };
    std::vector<CmdEntry> commands;
    for (Command* cmd : Server::getInstance().getCommandMap()->allCommands()) {
        if (!cmd->canUse(sender))
            continue; // ocultar los que no puede usar
        std::string desc = cmd->description();
        if (desc.empty())
            desc = "(no description)";
        commands.push_back(CmdEntry{cmd->name(), desc});
    }
    std::sort(commands.begin(), commands.end(), [](const CmdEntry& a, const CmdEntry& b) {
        return a.name < b.name;
    });

    // Paginate: 7 commands per page (like the reference server)
    int perPage = 7;
    int totalPages = (int)((commands.size() + perPage - 1) / perPage);
    if (totalPages < 1)
        totalPages = 1;
    if (pageNumber > totalPages)
        pageNumber = totalPages;

    // Header (the reference server style: "--- Showing help page X/Y ---")
    sender->sendMessage("--- Showing help page " + std::to_string(pageNumber) + "/" +
                        std::to_string(totalPages) + " ---");

    // Show commands for this page
    int start = (pageNumber - 1) * perPage;
    int end = std::min(start + perPage, (int)commands.size());
    for (int i = start; i < end; ++i) {
        sender->sendMessage("/" + commands[i].name + ": " + commands[i].description);
    }

    return true;
}

} // namespace
