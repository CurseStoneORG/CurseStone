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
// PlayerCreationEvent.h — fired inside Network::createPlayerForSession,
// right before a Player object is instantiated for a fresh RakNet
// session. It exposes the connection facts available at that moment
// (source address, port, the network interface owning the session) and
// lets an embedding application select WHICH implementation constructs
// the player.
//
// Custom player classes in C++: derive from cursestone::Player with a
// constructor taking (Server*, PlayerConfig) exactly like the base, and
// install a factory through setPlayerFactory(). The factory runs once;
// a factory that returns null is a hard error and the server falls back
// to the standard Player, logging a warning — creation never fails
// silently and never duplicates.
//
// This event is intentionally NOT cancellable: connection rejection
// belongs to PlayerPreLoginEvent, which runs later in the handshake
// with full identity data. Cancelling creation here would leave the
// RakNet session registered without a player object.
#pragma once
#include "../Event.h"
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <cstdint>
namespace cursestone {
class Player;
class Server;
struct PlayerConfig;
}

namespace cursestone::event::player {

class PlayerCreationEvent : public Event {
  public:
    // Builds the player object for this session. Implementations must
    // derive from cursestone::Player and accept (Server*, PlayerConfig).
    using PlayerFactory =
        std::function<std::shared_ptr<cursestone::Player>(cursestone::Server*,
                                                          cursestone::PlayerConfig&&)>;

    PlayerCreationEvent(const std::string& ip, uint16_t port, void* sourceInterface,
                        std::type_index baseClass)
        : ip_(ip), port_(port), source_interface_(sourceInterface), base_class_(baseClass) {
    }

    // Source address the session was accepted from.
    const std::string& ip() const {
        return ip_;
    }
    // Source port of the session.
    uint16_t port() const {
        return port_;
    }
    // Opaque pointer to the network source interface owning the session
    // (never dereferenced by the event system; the standard player gets
    // the same interface attached right after creation).
    void* sourceInterface() const {
        return source_interface_;
    }
    // The base class every valid player implementation derives from
    // (typeid(cursestone::Player)).
    const std::type_index& baseClass() const {
        return base_class_;
    }
    // The factory that will build the player. Null = the standard
    // cursestone::Player construction.
    const PlayerFactory& playerFactory() const {
        return factory_;
    }
    // Install a custom player implementation for this session.
    void setPlayerFactory(PlayerFactory factory) {
        factory_ = std::move(factory);
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerCreationEvent";
        return n;
    }

  private:
    std::string ip_;
    uint16_t port_;
    void* source_interface_;
    std::type_index base_class_;
    PlayerFactory factory_;
};

} // namespace cursestone::event::player
