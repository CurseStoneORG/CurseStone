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
#include "Defaults.h"
#include "defaults/StopCommand.h"
#include "defaults/HelpCommand.h"
#include "defaults/VersionCommand.h"
#include "defaults/KickCommand.h"
#include "defaults/KillCommand.h"
#include "defaults/TeleportCommand.h"
#include "defaults/GamemodeCommand.h"
#include "defaults/GiveCommand.h"
#include "defaults/SayCommand.h"
#include "defaults/ListCommand.h"
#include "defaults/OpCommand.h"
#include "defaults/DeopCommand.h"
#include "defaults/TimeCommand.h"
#include "defaults/WeatherCommand.h"
#include "defaults/SaveCommand.h"
#include "defaults/WorkbenchCommand.h"
#include "defaults/PluginsCommand.h"
#include "defaults/StatusCommand.h"
#include <memory>
#include <utility>
namespace cursestone {
void registerDefaultCommands(CommandMap& map) {
    map.registerCommand(std::make_unique<StopCommand>());
    map.registerCommand(std::make_unique<HelpCommand>());
    map.registerCommand(std::make_unique<VersionCommand>());
    map.registerCommand(std::make_unique<KickCommand>());
    map.registerCommand(std::make_unique<KillCommand>());
    map.registerCommand(std::make_unique<TeleportCommand>());
    map.registerCommand(std::make_unique<GamemodeCommand>());
    map.registerCommand(std::make_unique<GiveCommand>());
    map.registerCommand(std::make_unique<SayCommand>());
    map.registerCommand(std::make_unique<ListCommand>());
    map.registerCommand(std::make_unique<OpCommand>());
    map.registerCommand(std::make_unique<DeopCommand>());
    map.registerCommand(std::make_unique<TimeCommand>());
    map.registerCommand(std::make_unique<WeatherCommand>());
    map.registerCommand(std::make_unique<SaveCommand>());
    map.registerCommand(std::make_unique<WorkbenchCommand>());
    map.registerCommand(std::make_unique<PluginsCommand>());
    map.registerCommand(std::make_unique<StatusCommand>());

    // Native aliases: the software's own shortcuts, part of the command
    // infrastructure itself (never cursestone.yml). Each entry resolves at
    // dispatch time to its target command — permissions, arguments and
    // execution context are the target's, exactly like a plugin alias.
    // Shortcuts that used to be second registered instances (?, tp, gm)
    // are aliases too: one Command object per behavior, no duplicated
    // rows in /help.
    static constexpr std::pair<const char*, const char*> kNativeAliases[] = {
        {"ver", "version"},  {"about", "version"}, {"pl", "plugins"},
        {"?", "help"},       {"tp", "teleport"},   {"gm", "gamemode"},
    };
    for (const auto& [alias, target] : kNativeAliases)
        map.registerAlias(alias, target, nullptr);

    // No default /worlds command: world transport and management belong to
    // plugins now (server.loadWorld/unloadWorld + player.teleportWorld),
    // which can tailor the flow (selection UIs, permissions, warps) far
    // better than a built-in one-size-fits-all command.
}
} // namespace
