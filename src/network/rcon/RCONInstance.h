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
// RCONInstance — single listening socket + accept loop. C++.
// Stub for now: actual TCP listen + per-connection thread will be
// implemented in Phase 21 (full server wiring).
#pragma once

#include "../../CurseStone.h"
#include <memory>
#include <atomic>

namespace cursestone {
class Server;

namespace network {

class RCONInstance {
public:
    explicit RCONInstance(Server* server) : server_(server) {}
    ~RCONInstance() { shutdown(); }

    void start()  { running_ = true; }
    void shutdown() { running_ = false; }
    void tick()    { /* TODO: accept TCP connections, parse packets */ }

private:
    Server*       server_;
    std::atomic<bool> running_{false};
};

} // namespace network
} // namespace cursestone
