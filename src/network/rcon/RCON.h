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
// RCON — Source RCON server. Listens for password-authenticated
// console commands. C++ facade over a per-connection instance.
#pragma once

#include "../SourceInterface.h"
#include "../../CurseStone.h"
#include <string>
#include <memory>
#include <vector>

namespace cursestone {
class Server;

namespace network {

class RCONInstance;

class RCON : public SourceInterface {
  public:
    explicit RCON(Server* server);
    ~RCON() override;

    std::string getName() const override {
        return "rcon";
    }
    void start() override;
    void shutdown() override;
    void tick() override;

  private:
    Server* server_;
    std::unique_ptr<RCONInstance> instance_;
};

} // namespace network
} // namespace cursestone
