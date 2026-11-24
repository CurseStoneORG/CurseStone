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
// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerPreLoginEvent.h — fired while a connection is still completing
// its login handshake, BEFORE the server applies whitelist, bans and
// player-data loading. Plugins inspect the connection (address, port,
// client id, username) and may reject it.
//
// Timing: identity fields from the login packet are already stored on
// the player, but the player has NOT joined: it is not spawned, not
// listed and not counted as connected. Cancelling kicks the connection
// with the configured kick message (a default reason is used when the
// message is left empty) and the session never reaches logged-in state,
// so no half-registered player lingers behind.
//
// The event fires exactly once per login attempt: the handler that owns
// the handshake returns immediately on a repeat login packet, so no
// duplicate can reach listeners.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerPreLoginEvent : public PlayerEvent, public Cancellable {
  public:
    PlayerPreLoginEvent(cursestone::Player* p, const std::string& ip, uint16_t port,
                        const std::string& clientId)
        : PlayerEvent(p), ip_(ip), port_(port), clientId_(clientId) {
    }

    // Source address the connection came from.
    const std::string& ip() const {
        return ip_;
    }
    // Source port the connection came from.
    uint16_t port() const {
        return port_;
    }
    // RakNet client id reported by the login packet ("" when unknown).
    const std::string& clientId() const {
        return clientId_;
    }
    // Disconnect reason handed to a cancelled connection. Writable so a
    // listener can phrase its own rejection text.
    const std::string& kickMessage() const {
        return kick_message_;
    }
    void setKickMessage(const std::string& msg) {
        kick_message_ = msg;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerPreLoginEvent";
        return n;
    }

  private:
    std::string ip_;
    uint16_t port_;
    std::string clientId_;
    std::string kick_message_;
};

} // namespace cursestone::event::player
