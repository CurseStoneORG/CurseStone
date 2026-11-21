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
#pragma once

#include "command/Command.h"

namespace cursestone {

// /chestdebug — operator control of the chest diagnostic mode: on/off,
// status, per-position report, per-player trace list, clear.
class ChestDebugCommand : public Command {
  public:
    ChestDebugCommand()
        : Command("chestdebug", "Trace a chest from generation to the client (diagnosis)",
                  "/chestdebug <on|off|status|inspect|trace|clear>", "cursestone.command.chestdebug") {
    }
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};

} // namespace cursestone
