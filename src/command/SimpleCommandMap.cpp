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
    if (!cmd)
        return false;
    std::string n = cmd->name();
    auto [it, ok] = commands_.try_emplace(std::move(n), std::move(cmd));
    return ok;
}
bool SimpleCommandMap::overrideCommand(std::unique_ptr<Command> cmd) {
    if (!cmd)
        return false;
    std::string n = cmd->name();
    auto it = commands_.find(n);
    if (it == commands_.end()) {
        // Nothing to displace: behaves like a plain register.
        commands_.try_emplace(std::move(n), std::move(cmd));
        return true;
    }
    // Keep the displaced command as the fallback and swap the active one.
    // The old command object is destroyed here — its functions (execute,
    // canUse) are gone from this instant, so a plugin override fully
    // CANCELS the core behavior of /kick, /op, etc. If a fallback is
    // already chained (core -> A -> B), the previous NON-original command
    // is dropped and the ORIGINAL stays as the single fallback, so the
    // unloads always restore the true core command.
    if (overridden_.find(n) == overridden_.end()) {
        overridden_[n] = std::move(it->second);
    } else {
        it->second.reset();
    }
    it->second = std::move(cmd);
    return true;
}
bool SimpleCommandMap::dispatch(CommandSender* sender, const std::string& cmd_line_arg) {
    // La consola entrega la linea TAL CUAL ("/op Dr1xy"), mientras que
    // Player::handleChat() ya se quita la barra a los jugadores. Sin este
    // recorte, un "/op" escrito en la consola caia en "Unknown command: /op"
    // y NUNCA se aplicaba: el admin lo escribia, el log decia que la linea
    // era desconocida, y el jugador seguia sin ser op — exactamente el
    // sintoma de "le pongo /op y aun asi no tengo permisos".
    // Se normaliza AQUI (y no en cada caller) para que valga igual para
    // consola, jugadores, RCON y cualquier comando que llame a dispatch().
    std::string cmd_line = cmd_line_arg;
    size_t start = cmd_line.find_first_not_of(" \t/");
    if (start == std::string::npos)
        return false; // linea vacia o solo "///"
    cmd_line.erase(0, start);

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
    for (const auto& [n, cmd] : commands_)
        out.push_back(cmd.get());
    return out;
}
size_t SimpleCommandMap::unregisterByOwner(void* owner) {
    size_t removed = 0;
    for (auto it = commands_.begin(); it != commands_.end();) {
        if (it->second->owner() == owner) {
            // If this command displaced an original (override), restore it
            // so the core (or the other plugin) keeps working after the
            // unload — the fallback takes the name back.
            auto fb = overridden_.find(it->first);
            if (fb != overridden_.end()) {
                it->second = std::move(fb->second);
                overridden_.erase(fb);
                ++it;
            } else {
                it = commands_.erase(it);
            }
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}
} // namespace
