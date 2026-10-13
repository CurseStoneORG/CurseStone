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
// DataPacketSendEvent.h — ported from cn.nukkit.event.server.DataPacketSendEvent
//
// Fired before the server sends a packet to a player. Cancellable;
// cancelling the event prevents the packet from being sent.
#pragma once
#include "ServerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { class Player; }

namespace cursestone::event::server {

class DataPacketSendEvent : public ServerEvent, public Cancellable {
public:
    void* packet;
    cursestone::Player* player;

    DataPacketSendEvent(cursestone::Player* p, void* pk)
        : packet(pk), player(p) {}

    const std::string& eventName() const override {
        static std::string n = "DataPacketSendEvent"; return n;
    }
};

} // namespace cursestone::event::server
