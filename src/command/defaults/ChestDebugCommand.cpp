/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | |  __/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ChestDebugCommand.h"

#include "diagnostics/ChestDebug.h"

#include <cerrno>
#include <cstdlib>

namespace cursestone {
namespace {

bool parseInt(const char* s, int& out) {
    if (!s || !*s)
        return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < -1000000000L || v > 1000000000L)
        return false;
    out = static_cast<int>(v);
    return true;
}

void sendAll(CommandSender* sender, const std::vector<std::string>& lines) {
    for (const auto& line : lines)
        sender->sendMessage(line);
}

} // namespace

bool ChestDebugCommand::execute(CommandSender* sender, const std::string&,
                                const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "status") {
        sendAll(sender, diagnostics::ChestDebug::statusLines());
        return true;
    }
    const std::string& sub = args[0];
    if (sub == "on") {
        diagnostics::ChestDebug::setEnabled(true);
        sender->sendMessage("[ChestDebug] ON — walk past / tap chests to build traces; use "
                            "inspect, trace and status");
        return true;
    }
    if (sub == "off") {
        diagnostics::ChestDebug::setEnabled(false);
        sender->sendMessage("[ChestDebug] OFF (traces cleared)");
        return true;
    }
    if (sub == "clear") {
        diagnostics::ChestDebug::clear();
        sender->sendMessage("[ChestDebug] traces cleared");
        return true;
    }
    if (sub == "inspect") {
        int x = 0, y = 0, z = 0;
        if (args.size() != 4 || !parseInt(args[1].c_str(), x) || !parseInt(args[2].c_str(), y) ||
            !parseInt(args[3].c_str(), z)) {
            sender->sendMessage("[ChestDebug] usage: /chestdebug inspect <x> <y> <z>");
            return true;
        }
        sendAll(sender, diagnostics::ChestDebug::reportLines(x, y, z));
        return true;
    }
    if (sub == "trace") {
        if (args.size() != 2) {
            sender->sendMessage("[ChestDebug] usage: /chestdebug trace <player>");
            return true;
        }
        sendAll(sender, diagnostics::ChestDebug::playerTraceLines(args[1]));
        return true;
    }
    sender->sendMessage("[ChestDebug] usage: /chestdebug <on|off|status|inspect|trace|clear>");
    sender->sendMessage("[ChestDebug]  on/off    — toggle the chest instrumentation");
    sender->sendMessage("[ChestDebug]  status    — mode state, counters, recent findings");
    sender->sendMessage("[ChestDebug]  inspect   — full report for one chest position");
    sender->sendMessage("[ChestDebug]  trace     — chest traces that name a player");
    sender->sendMessage("[ChestDebug]  clear     — drop all traces");
    return true;
}

} // namespace cursestone
