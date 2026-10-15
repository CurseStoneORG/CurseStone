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
// SourceInterface — abstract network interface. C++.
// A Network may have multiple SourceInterfaces (RakLib for client traffic,
// QueryHandler for GS4 status, RCON for remote console).
#pragma once

#include <string>
#include <cstdint>

namespace cursestone::network {

class SourceInterface {
  public:
    virtual ~SourceInterface() = default;
    virtual std::string getName() const = 0;
    virtual void start() = 0;
    virtual void shutdown() = 0;
    virtual void tick() = 0;
};

} // namespace cursestone::network
