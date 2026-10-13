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
#pragma once
#include "Command.h"
#include <string>
#include <vector>
namespace cursestone {
class CommandMap {
public:
    virtual ~CommandMap() = default;
    virtual bool registerCommand(std::unique_ptr<Command> cmd) = 0;
    virtual bool dispatch(CommandSender* sender, const std::string& cmd_line) = 0;
    virtual Command* getCommand(const std::string& name) const = 0;
    // Todos los comandos registrados (para /help dinámico). Los alias
    // (gm, tp, ?...) son comandos propios y aparecen como tales.
    virtual std::vector<Command*> allCommands() const { return {}; }
    // Removes every command whose Command::owner() == owner (a plugin's
    // LuaCommand objects, right before /plugins reload destroys their
    // lua_State). Returns how many commands were removed. The default is
    // a no-op so lightweight/mock maps don't have to care.
    virtual size_t unregisterByOwner(void* owner) {
        (void)owner;
        return 0;
    }
};
} // namespace
