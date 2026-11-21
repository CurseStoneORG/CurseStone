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
#include "SimpleCommandMap.h"
#include "utils/Utils.hpp"
#include <algorithm>
#include <cctype>
namespace cursestone {
namespace {
// Alias chains are followed at most this many hops before the line is
// declared unknown — a cyclic pair of aliases can never hang a dispatch.
constexpr int kAliasMaxHops = 8;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}
} // namespace

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
bool SimpleCommandMap::registerAlias(const std::string& alias, const std::string& target,
                                     void* owner) {
    // Names are stored lowercased like every command name (the map looks up
    // by exact string); the fixed-argument tail keeps its original casing.
    std::string a = toLower(alias);
    std::string t = toLower(target);
    const size_t sp = t.find(' ');
    const std::string tName = (sp == std::string::npos) ? t : t.substr(0, sp);
    if (a.empty() || tName.empty())
        return false;
    // A real command always wins: the alias name must be free both as a
    // command and as an alias, and may not point at itself.
    if (commands_.count(a) || aliases_.count(a) || a == tName)
        return false;
    // Store the target with its command name lowercased (the dispatch
    // lookup is by exact string) and the fixed-argument tail as written.
    std::string normalized = tName + (sp == std::string::npos ? "" : target.substr(sp));
    aliases_.emplace(std::move(a), AliasEntry{std::move(normalized), owner});
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
    Command* cmd = nullptr;
    if (it != commands_.end()) {
        cmd = it->second.get();
    } else {
        // Exact miss: walk the alias table. An alias expands to "target
        // [fixed args]"; chains are followed until a real command appears
        // (bounded so a cyclic pair cannot hang a dispatch) and the fixed
        // arguments accumulate outer-first, ahead of the caller's own.
        std::vector<std::string> fixed;
        std::string cur = name;
        for (int hops = 0; hops < kAliasMaxHops; ++hops) {
            auto ait = aliases_.find(cur);
            if (ait == aliases_.end())
                break;
            const std::string& target = ait->second.target;
            const size_t tsp = target.find(' ');
            cur = (tsp == std::string::npos) ? target : target.substr(0, tsp);
            if (tsp != std::string::npos) {
                std::vector<std::string> tail = utils::split(target.substr(tsp + 1), ' ');
                fixed.insert(fixed.end(), std::make_move_iterator(tail.begin()),
                             std::make_move_iterator(tail.end()));
            }
            auto cit = commands_.find(cur);
            if (cit != commands_.end()) {
                cmd = cit->second.get();
                break;
            }
        }
        if (!cmd) {
            sender->sendMessage("Unknown command: " + name);
            return false;
        }
        args.insert(args.begin(), std::make_move_iterator(fixed.begin()),
                    std::make_move_iterator(fixed.end()));
    }
    // --- CONTROL DE PERMISOS ---
    //
    // Single funnel for every command (aliases included: an alias to an
    // op-only command still requires op). The permission check lives in
    // Command::canUse() (virtual): los comandos normales usan el nodo de
    // permiso declarado, y los comandos de plugins (LuaCommand) pueden
    // define its own rule (op / custom node / public). A command with no
    // declared node (empty permission_) stays public:
    // help, list, version.
    if (!cmd->canUse(sender)) {
        sender->sendMessage("You do not have permission to use this command.");
        return false;
    }
    try {
        return cmd->execute(sender, name, args);
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
    // The plugin's aliases die with its commands — an alias that outlives
    // its owner would dispatch into a plugin the server no longer has.
    for (auto ait = aliases_.begin(); ait != aliases_.end();) {
        if (ait->second.owner == owner) {
            ait = aliases_.erase(ait);
            ++removed;
        } else {
            ++ait;
        }
    }
    return removed;
}
} // namespace
