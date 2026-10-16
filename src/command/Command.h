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
// Command — base class for any registered command. Mirrors the reference server
// /command/Command. Subclasses define execute() which is invoked by
// SimpleCommandMap when a player or the console types /<name>.
#pragma once
#include "CommandSender.h"
#include <string>
#include <vector>
namespace cursestone {
class Command {
  public:
    // `permission` es el nodo de permiso que el emisor debe tener para
    // poder ejecutar este comando. VACIO = cualquiera puede ejecutarlo
    // (help, list, version...). Un nodo como "cursestone.command.stop"
    // loMarco SimpleCommandMap::dispatch() antes de invocar execute(),
    // de modo que un comando sin permiso NUNCA llega al execute() — ni
    // siquiera si un comando forgets de revisar isOp() en su cuerpo.
    Command(std::string name, std::string description = "", std::string usage = "",
            std::string permission = "")
        : name_(std::move(name)), description_(std::move(description)), usage_(std::move(usage)),
          permission_(std::move(permission)) {
    }
    virtual ~Command() = default;
    virtual bool execute(CommandSender* sender, const std::string& label,
                         const std::vector<std::string>& args) = 0;
    const std::string& name() const {
        return name_;
    }
    const std::string& description() const {
        return description_;
    }
    const std::string& usage() const {
        return usage_;
    }
    // Nodo requerido, o "" si es público.
    const std::string& permission() const {
        return permission_;
    }
    bool isPublic() const {
        return permission_.empty();
    }
    // ¿Puede este emisor usar el comando? SimpleCommandMap::dispatch() lo
    // consulta ANTES de execute() y /help lo usa para filtrar la lista.
    // Por defecto: público, o el emisor tiene el nodo de permiso pedido.
    // (LuaCommand lo sobreescribe: op / nodo custom con fallback a op.)
    virtual bool canUse(CommandSender* sender) const {
        return isPublic() || sender->hasPermission(permission_);
    }
    // Owner tag for commands registered at RUNTIME by plugins: LuaCommand
    // returns its LuaPlugin*, core commands return nullptr. Used by
    // /plugins reload to unregister a plugin's commands BEFORE its
    // lua_State is destroyed (the command holds Lua refs).
    virtual void* owner() const {
        return nullptr;
    }

  private:
    std::string name_, description_, usage_, permission_;
};
} // namespace
