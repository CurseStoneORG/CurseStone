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
#include "CommandMap.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
namespace cursestone {
class SimpleCommandMap : public CommandMap {
public:
    bool registerCommand(std::unique_ptr<Command> cmd) override;
    // Reemplaza un comando existente por este (o lo registra normal si
    // el nombre no existia). El comando desplazado queda guardado como
    // fallback en overridden_ y unregisterByOwner() lo restaura cuando
    // el plugin que lo piso se descarga — un /plugins reload deja el
    // nucleo siempre con sus comandos originales. Devuelve false solo
    // si cmd es null.
    bool overrideCommand(std::unique_ptr<Command> cmd) override;
    bool dispatch(CommandSender* sender, const std::string& cmd_line) override;
    Command* getCommand(const std::string& name) const override;
    std::vector<Command*> allCommands() const override;
    // Borra todos los comandos cuyo Command::owner() == owner (los
    // LuaCommand de un plugin, antes de que /plugins reload destruya su
    // lua_State). Si un comando borrado habia pisado a otro (override),
    // el desplazado se restaura en su lugar. Devuelve cuántos borró.
    size_t unregisterByOwner(void* owner) override;
    size_t size() const { return commands_.size(); }
private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;
    // comandos desplazados por override: nombre -> el comando original
    // (nucleo o de otro plugin) que vuelve cuando el que lo piso se va.
    std::unordered_map<std::string, std::unique_ptr<Command>> overridden_;
    std::vector<std::string> aliases_;
};
} // namespace
