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
#include "SimpleCommandMap.h"
#include "utils/Utils.hpp"
namespace cursestone {
bool SimpleCommandMap::registerCommand(std::unique_ptr<Command> cmd) {
    if (!cmd) return false;
    std::string n = cmd->name();
    auto [it, ok] = commands_.try_emplace(std::move(n), std::move(cmd));
    return ok;
}
bool SimpleCommandMap::dispatch(CommandSender* sender, const std::string& cmd_line) {
    // Split on first space: name + args
    auto sp = cmd_line.find(' ');
    std::string name = (sp == std::string::npos) ? cmd_line : cmd_line.substr(0, sp);
    std::vector<std::string> args;
    if (sp != std::string::npos) {
        std::string rest = cmd_line.substr(sp + 1);
        args = utils::split(rest, ' ');
    }
    auto it = commands_.find(name);
    if (it == commands_.end()) {
        sender->sendMessage("Unknown command: " + name);
        return false;
    }
    // --- CONTROL DE PERMISOS ---
    //
    // Punto único por el que pasan todos los comandos. El chequeo vive en
    // Command::canUse() (virtual): los comandos normales usan el nodo de
    // permiso declarado, y los comandos de plugins (LuaCommand) pueden
    // definir su propio criterio (op / nodo custom / público). Un comando
    // sin nodo declarado (permission_ vacío) sigue siendo público:
    // help, list, version.
    Command& cmd = *it->second;
    if (!cmd.canUse(sender)) {
        sender->sendMessage("You do not have permission to use this command.");
        return false;
    }
    try {
        return it->second->execute(sender, name, args);
    } catch (const std::exception& e) {
        sender->sendMessage("Error executing command: " + std::string(e.what()));
        return false;
    }
}
Command* SimpleCommandMap::getCommand(const std::string& name) const {
    auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : it->second.get();
}
std::vector<Command*> SimpleCommandMap::allCommands() const {
    std::vector<Command*> out;
    out.reserve(commands_.size());
    for (const auto& [n, cmd] : commands_) out.push_back(cmd.get());
    return out;
}
size_t SimpleCommandMap::unregisterByOwner(void* owner) {
    size_t removed = 0;
    for (auto it = commands_.begin(); it != commands_.end();) {
        if (it->second->owner() == owner) {
            it = commands_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}
} // namespace
