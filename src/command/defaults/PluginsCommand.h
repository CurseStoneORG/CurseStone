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
// PluginsCommand — /plugins: lista los plugins Lua cargados con su
// versión y estado. Sigue el mismo patrón que /version o /list.
// Es un comando público (permission vacío): cualquier jugador puede VER
// la lista. El subcomando /plugins reload (recarga todos los plugins en
// caliente) queda reservado a ops y consola.
#pragma once
#include "command/Command.h"

namespace cursestone {

class PluginsCommand : public Command {
public:
    PluginsCommand()
        : Command("plugins", "List loaded plugins (reload: op only)",
                  "/plugins [reload]", "") {}  // "" = público
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};

}
