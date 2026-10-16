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
namespace cursestone {
void registerDefaultCommands(CommandMap& map) {
    map.registerCommand(std::make_unique<StopCommand>());
    map.registerCommand(std::make_unique<HelpCommand>("help"));
    map.registerCommand(std::make_unique<HelpCommand>("?"));
    map.registerCommand(std::make_unique<VersionCommand>());
    map.registerCommand(std::make_unique<KickCommand>());
    map.registerCommand(std::make_unique<KillCommand>());
    map.registerCommand(std::make_unique<TeleportCommand>("teleport"));
    map.registerCommand(std::make_unique<TeleportCommand>("tp"));
    map.registerCommand(std::make_unique<GamemodeCommand>("gamemode"));
    map.registerCommand(std::make_unique<GamemodeCommand>("gm"));
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
}
} // namespace
