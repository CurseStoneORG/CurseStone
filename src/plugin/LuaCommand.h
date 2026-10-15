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
// LuaCommand — un comando registrado desde Lua via commands.register().
//
// PERMISOS: el campo `permission` de commands.register decide QUIÉN puede
// ejecutarlo y se chequea en canUse() (llamado por SimpleCommandMap::
// dispatch() ANTES de execute(), y por /help para filtrar la lista):
//
//   PUBLIC  ("default"/ausente)  -> cualquiera
//   OP      ("op"/"admin")       -> ops, consola, o nodo cursestone.command.<name>
//   NODE    (cualquier otro string) -> quien tenga el nodo concedido
//                                   (player.setPermission) o un op
//
// ⚠️ HILOS: execute() puede correr en el hilo del CommandReader (consola)
// o en el hilo principal (jugador). Lua NO es thread-safe, así que
// execute() NUNCA llama a Lua directamente: copia los datos del sender
// (el puntero NO sobrevive: en consola es una variable local del
// CommandReader), encola un CallbackTask con ServerScheduler::scheduleSync
// y el callback corre en el próximo tick, en el hilo principal.
#pragma once
#include "command/Command.h"
#include "LuaPlugin.h"

namespace cursestone::plugin {

class LuaCommand : public Command {
  public:
    enum class PermMode { PUBLIC, OP, NODE };

    LuaCommand(LuaPlugin* plugin, int fnRef, std::string name, std::string description,
               std::string usage, PermMode mode, std::string node)
        : Command(std::move(name), std::move(description), std::move(usage), ""), plugin_(plugin),
          fnRef_(fnRef), mode_(mode), node_(std::move(node)) {
    }

    // Criterio de permiso de ESTE comando (ver doc de la clase).
    bool canUse(CommandSender* sender) const override;

    // Owner para SimpleCommandMap::unregisterByOwner: /plugins reload
    // borra los comandos de este plugin ANTES de cerrar su lua_State
    // (los refs de fnRef_ apuntan al registry de ese state).
    void* owner() const override {
        return plugin_;
    }

    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;

  private:
    LuaPlugin* plugin_;
    int fnRef_;
    PermMode mode_;
    std::string node_; // solo en modo NODE
};

}
