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
// WorkbenchCommand.cpp — /workbench command implementation.
#include "WorkbenchCommand.h"
#include "workbench/WorkbenchManager.h"
#include "Server.h"
#include "utils/TextColor.h"

namespace cursestone {

bool WorkbenchCommand::execute(CommandSender* sender, const std::string&,
                                const std::vector<std::string>& args) {
    // Only ops can use this command.
    if (!sender->isOp()) {
        sender->sendMessage(TC::RED + "You don't have permission to use this command." + TC::RESET);
        return true;
    }

    std::string subcommand = args.empty() ? "status" : args[0];

    if (subcommand == "start") {
        WorkbenchManager::get().start();
        sender->sendMessage(TC::GREEN + "[Workbench] Diagnostics started." + TC::RESET);
        sender->sendMessage(TC::YELLOW + "Use /workbench stop to export reports." + TC::RESET);
    } else if (subcommand == "stop") {
        sender->sendMessage(TC::YELLOW + "[Workbench] Stopping and exporting..." + TC::RESET);
        WorkbenchManager::get().stop();
    } else if (subcommand == "report") {
        sender->sendMessage(TC::YELLOW + "[Workbench] Exporting snapshot..." + TC::RESET);
        WorkbenchManager::get().report();
    } else if (subcommand == "status") {
        WorkbenchManager::get().status();
    } else {
        sender->sendMessage(TC::GOLD + "=== Workbench ===" + TC::RESET);
        sender->sendMessage(TC::WHITE + "/workbench start  " + TC::GRAY + "- Start all diagnostic modules" + TC::RESET);
        sender->sendMessage(TC::WHITE + "/workbench stop   " + TC::GRAY + "- Stop and export reports" + TC::RESET);
        sender->sendMessage(TC::WHITE + "/workbench status " + TC::GRAY + "- Show current status" + TC::RESET);
        sender->sendMessage(TC::WHITE + "/workbench report " + TC::GRAY + "- Export snapshot without stopping" + TC::RESET);
    }
    return true;
}

} // namespace cursestone
