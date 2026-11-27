/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | |  \__ \/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
| |___/|_| |_|  |___/\___|\____/ \__\___/|_| |_|\___|

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

// /effect — the vanilla effect command, the third application path besides
// drinking and splashing. Mirrors the 0.15.x syntax:
//   /effect <player> <effect(id|name)> [seconds] [amplifier]
//   /effect <player> clear
// A duration of 0 removes the effect (vanilla rule), "clear" removes all.
class EffectCommand : public Command {
  public:
    EffectCommand()
        : Command("effect", "Add or remove potion effects",
                  "/effect <player> <effect> [seconds] [amplifier] | /effect <player> clear",
                  "cursestone.command.effect") {
    }

    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};

} // namespace cursestone
