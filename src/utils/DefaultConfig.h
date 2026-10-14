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
// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file DefaultConfig.h
 * @brief Built-in default contents of server.properties and cursestone.yml.
 *
 * The defaults are compiled into the binary, so first-run config creation
 * does not depend on any external file or working directory.
 */
#pragma once

#include <string>
#include <vector>

namespace cursestone::utils {

/** Basic settings (network, players, gameplay, world name and seed). */
extern const char* const kDefaultServerProperties;

/** Advanced settings (workers, autosave, memory, world format/generator). */
extern const char* const kDefaultCursestoneYml;

/**
 * @brief Creates `dataPath` and any missing config file from the defaults.
 *
 * Existing files are never overwritten.
 *
 * @param dataPath  server data directory.
 * @param created   optional; receives the names of the files written.
 * @return true if both config files exist when the call returns.
 */
bool writeDefaultConfigs(const std::string& dataPath,
                         std::vector<std::string>* created = nullptr);

} // namespace cursestone::utils
