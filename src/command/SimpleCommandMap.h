/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
    // Registra un alias: teclear /alias despacha /target. target puede
    // llevar argumentos prefijados ("gamemode 1"), y los argumentos que
    // el emisor escriba se AGREGAN al final. Los comandos reales ganan
    // siempre: un nombre ocupado por un comando no puede ser alias, y la
    // resolucion solo se intenta cuando la busqueda exacta fallo.
    bool registerAlias(const std::string& alias, const std::string& target,
                       void* owner) override;
    // Borra UN alias registrado por el mismo owner (nullptr = los alias
    // nativos del software). Un plugin nunca puede soltar un alias ajeno
    // o nativo por esta via. true si existia y ya no esta.
    bool unregisterAlias(const std::string& alias, void* owner) override;
    bool dispatch(CommandSender* sender, const std::string& cmd_line) override;
    Command* getCommand(const std::string& name) const override;
    std::vector<Command*> allCommands() const override;
    // Borra todos los comandos cuyo Command::owner() == owner (los
    // LuaCommand de un plugin, antes de que /plugins reload destruya su
    // lua_State) y tambien sus alias. Si un comando borrado habia pisado
    // a otro (override), the displaced one is restored in place. Returns
    // how many commands and aliases were removed.
    size_t unregisterByOwner(void* owner) override;
    size_t size() const {
        return commands_.size();
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;
    // comandos desplazados por override: nombre -> el comando original
    // (nucleo o de otro plugin) que vuelve cuando el que lo piso se va.
    std::unordered_map<std::string, std::unique_ptr<Command>> overridden_;
    // alias -> linea destino ("comando [argumentos prefijados]") + dueno
    // (nullptr = el software: alias nativos). Resueltos en dispatch()
    // solo cuando el nombre exacto no es un comando.
    struct AliasEntry {
        std::string target;
        void* owner;
    };
    std::unordered_map<std::string, AliasEntry> aliases_;
};
} // namespace
