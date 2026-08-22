// SPDX-License-Identifier: LGPL-3.0-or-later
// Network.cpp — top-level network manager implementation.
//
// Receives raw bytes from RakLibInterface, decodes BatchPacket,
// dispatches sub-packets to the appropriate Player.
#include "Network.h"
#include "../Server.h"
#include "../Player.h"
#include "../utils/Utils.h"
#include "../utils/MainLogger.hpp"
#include "protocol/BatchPacket.h"
#include "protocol/LoginPacket.h"
#include "protocol/DataPacket.h"
#include "protocol/PacketPool.h"

#include <sstream>
#include <iostream>

namespace mcppeserver {
namespace network {

void Network::registerInterface(std::unique_ptr<SourceInterface> iface) {
    std::string name = iface->getName();
    nameToIndex_[name] = interfaces_.size();
    interfaces_.push_back(std::move(iface));
}

SourceInterface* Network::getInterface(const std::string& name) {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return nullptr;
    return interfaces_[it->second].get();
}

AdvancedSourceInterface* Network::getInterfaceAdvanced(const std::string& name) {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return nullptr;
    return dynamic_cast<AdvancedSourceInterface*>(interfaces_[it->second].get());
}

static std::string sessionKey(const std::string& ip, uint16_t port) {
    std::ostringstream o;
    o << ip << ':' << port;
    return o.str();
}

void Network::onSessionOpen(const std::string& ip, uint16_t port,
                              int64_t clientId) {
    (void)clientId;
    if (isBlocked(ip)) {
        if (auto* raklib = getRakLib()) raklib->closeSession(ip, port, "blocked");
        return;
    }
    // Don't create the Player yet — wait for the LoginPacket (PMMP semantics).
    // The actual session creation happens in processIncoming when we see the
    // first packet that looks like a LoginPacket (raw or wrapped in Batch).
}

void Network::onSessionClose(const std::string& ip, uint16_t port,
                                const std::string& reason) {
    std::string key = sessionKey(ip, port);
    auto it = sessions_.find(key);
    if (it != sessions_.end()) {
        Player* p = it->second;
        sessions_.erase(it);
        if (p) p->close(reason);
    }
}

void Network::processIncoming(const std::string& ip, uint16_t port,
                                const uint8_t* data, size_t len) {
    if (isBlocked(ip) || !data || len == 0) return;

    // CRITICAL: MCPE clients prepend 0xFE to ALL game packets (BATCH and
    // non-BATCH) to distinguish them from RakNet control packets. PMMP's
    // RakLibInterface.php does the same: chr(0xfe) . $packet->buffer.
    // Without stripping this byte, the BATCH packet (0x06) is never
    // recognized and the entire login flow silently breaks — the client
    // can see the MOTD but can never connect, and nothing appears in the
    // server log.
    if (data[0] == 0xFE && len > 1) {
        data++;
        len--;
    }

    uint8_t pid = data[0];

    if (pid == protocol::BATCH_PACKET) {
        protocol::BatchPacket bp;
        if (!bp.decode(data + 1, len - 1)) {
            utils::MainLogger::get().warn("Malformed BatchPacket from " + ip);
            if (auto* r = getRakLib()) r->closeSession(ip, port, "bad batch");
            return;
        }
        const auto& payload = bp.payload();
        size_t off = 0;
        while (off + 4 <= payload.size()) {
            uint32_t sl = ((uint32_t)payload[off]   << 24) |
                          ((uint32_t)payload[off+1] << 16) |
                          ((uint32_t)payload[off+2] << 8)  |
                          ((uint32_t)payload[off+3]);
            off += 4;
            if (off + sl > payload.size()) {
                utils::MainLogger::get().warn("Truncated sub-packet in batch from " + ip);
                break;
            }
            dispatchSingle(ip, port, payload.data() + off, sl);
            off += sl;
        }
        return;
    }

    // Non-batched single packet (rare; mostly LoginPacket before encryption).
    dispatchSingle(ip, port, data, len);
}

void Network::dispatchSingle(const std::string& ip, uint16_t port,
                                const uint8_t* data, size_t len) {
    if (len == 0) return;
    uint8_t pid = data[0];

    Player* p = getPlayer(ip, port);
    if (!p) {
        // Only LoginPacket may create a new session.
        if (pid != protocol::LOGIN_PACKET) {
            // Not a login and no session — drop silently.
            return;
        }
        if (!createPlayerForSession(ip, port)) return;
        p = getPlayer(ip, port);
        if (!p) return;
    }

    // Decode the body (everything after the leading ID byte) via the
    // PacketPool registry and forward to the Player's dispatcher.
    auto pkt = decodePacketById(pid, data + 1, len - 1);
    if (!pkt) return;
    p->handleDataPacket(*pkt);
}

std::shared_ptr<protocol::DataPacket> Network::decodePacketById(uint8_t pid,
        const uint8_t* body, size_t bodyLen) {
    // Instantiate the right subclass via the PacketPool registry.
    auto pkt = protocol::PacketPool::get().create(pid);
    if (!pkt) return nullptr;
    if (!pkt->decode(body, bodyLen)) {
        utils::MainLogger::get().warn("Failed to decode packet id=" +
            std::to_string((int)pid));
        return nullptr;
    }
    return pkt;
}

bool Network::createPlayerForSession(const std::string& ip, uint16_t port) {
    PlayerConfig cfg;
    cfg.ip   = ip;
    cfg.port = port;
    cfg.gamemode        = server_->gamemode();
    cfg.view_distance   = server_->viewDistance();
    auto p = std::make_shared<Player>(server_, std::move(cfg));
    if (auto* r = getRakLib()) p->setSourceInterface(r);
    server_->addPlayer(p);
    registerSession(ip, port, p.get());
    return true;
}

Player* Network::getPlayer(const std::string& ip, uint16_t port) {
    auto it = sessions_.find(sessionKey(ip, port));
    return it != sessions_.end() ? it->second : nullptr;
}

void Network::registerSession(const std::string& ip, uint16_t port, Player* p) {
    sessions_[sessionKey(ip, port)] = p;
}
void Network::unregisterSession(Player* p) {
    if (!p) return;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->second == p) { sessions_.erase(it); return; }
    }
}

void Network::addPlayer(std::unique_ptr<Player> /*p*/) {
    // Network doesn't own the player — Server::addPlayer does.
}

void Network::removePlayer(Player* p) {
    if (!p) return;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->second == p) { sessions_.erase(it); return; }
    }
}

void Network::tick() {
    for (auto& iface : interfaces_) {
        iface->tick();
    }
}

void Network::blockAddress(const std::string& ip, uint64_t timeoutMs) {
    blockedUntil_[ip] = (timeoutMs == 0)
        ? UINT64_MAX
        : mcp_utils_time_ms() + timeoutMs;
}

bool Network::isBlocked(const std::string& ip) const {
    auto it = blockedUntil_.find(ip);
    if (it == blockedUntil_.end()) return false;
    if (it->second == UINT64_MAX) return true;
    return mcp_utils_time_ms() < it->second;
}

} // namespace network
} // namespace mcppeserver
