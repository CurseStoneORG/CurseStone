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
// Network — top-level network manager. C++.
// Owns the RakLibInterface (UDP), QueryHandler (GS4), RCON listener.
// Routes incoming packets to the right Player.
#pragma once

#include "SourceInterface.h"
#include "AdvancedSourceInterface.h"
#include "protocol/Info.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <cstdint>

namespace cursestone {

class Player;
class Server;
struct PlayerConfig;

namespace network::protocol {
class DataPacket;
}

namespace network {

class Network {
  public:
    explicit Network(Server* server) : server_(server) {
    }

    // Register / lookup interfaces by string name ("raklib", "query", "rcon").
    void registerInterface(std::unique_ptr<SourceInterface> iface);
    SourceInterface* getInterface(const std::string& name);
    AdvancedSourceInterface* getInterfaceAdvanced(const std::string& name);
    AdvancedSourceInterface* getRakLib() {
        return getInterfaceAdvanced("raklib");
    }

    // Called by RakLibInterface when a packet arrives. Dispatches to player.
    void processIncoming(const std::string& ip, uint16_t port, const uint8_t* data, size_t len);

    // Called by RakLibInterface when a session opens/closes.
    void onSessionOpen(const std::string& ip, uint16_t port, int64_t clientId);
    void onSessionClose(const std::string& ip, uint16_t port, const std::string& reason);

    // Look up or create a Player for an incoming session.
    Player* getPlayer(const std::string& ip, uint16_t port);
    void registerSession(const std::string& ip, uint16_t port, std::shared_ptr<Player> p);
    void unregisterSession(Player* p);
    void addPlayer(std::unique_ptr<Player> p);
    void removePlayer(Player* p);

    // Update average packet receive rate for diagnostics.
    void tick();

    // IP ban list (mirrors the reference server's Network::blockAddress).
    void blockAddress(const std::string& ip, uint64_t timeoutMs = 0);
    bool isBlocked(const std::string& ip) const;

  private:
    // Dispatches a single (already-extracted) sub-packet body to its Player.
    void dispatchSingle(const std::string& ip, uint16_t port, const uint8_t* data, size_t len);
    // Instantiates the right DataPacket subclass by ID and decodes the body.
    // `who` is the "ip:port" the packet came from, used for diagnostics.
    std::shared_ptr<protocol::DataPacket> decodePacketById(uint8_t pid, const uint8_t* body,
                                                           size_t bodyLen, const std::string& who);
    // Creates a Player for a new (ip,port) session. Returns true on success.
    bool createPlayerForSession(const std::string& ip, uint16_t port);

    Server* server_;
    std::vector<std::unique_ptr<SourceInterface>> interfaces_;
    std::unordered_map<std::string, size_t> nameToIndex_;
    // Session key = "ip:port". weak_ptr so we don't keep Players alive
    // after Server::tick() reaps them from players_. Using a raw Player*
    // here caused a use-after-free: the network thread called p->close()
    // on a Player that the main thread had already destroyed.
    std::unordered_map<std::string, std::weak_ptr<Player>> sessions_;
    std::unordered_map<std::string, uint64_t> blockedUntil_;
};

} // namespace network
} // namespace cursestone
