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
#include "../diagnostics/Diagnostics.h"
#include "protocol/BatchPacket.h"
#include "protocol/LoginPacket.h"
#include "protocol/DataPacket.h"
#include "protocol/PacketPool.h"

#include <sstream>
#include <iostream>

namespace cursestone {
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
        // Lock the weak_ptr to get a safe shared_ptr. If the Player was
        // already destroyed by the main thread, lock() returns nullptr
        // and we skip the close() call (it would be a use-after-free).
        auto sp = it->second.lock();
        sessions_.erase(it);
        if (sp) {
            sp->close(reason);
        }
    }
}

void Network::processIncoming(const std::string& ip, uint16_t port,
                                const uint8_t* data, size_t len) {
    if (isBlocked(ip) || !data || len == 0) return;

    (void)server_; // adv logging removed

    // CRITICAL: MCPE clients prepend 0xFE to ALL game packets.
    if (data[0] == 0xFE && len > 1) {
        data++;
        len--;
    }

    uint8_t pid = data[0];

    // Packet debug logging disabled to keep console clean.
    // Only log errors (malformed packets, etc.).

    if (pid == protocol::BATCH_PACKET) {
        protocol::BatchPacket bp;
        if (!bp.decode(data + 1, len - 1)) {
            // Log first bytes of compressed data for debugging
            char hex[128] = {0};
            const uint8_t* cd = data + 1;
            size_t cdlen = len - 1;
            if (cdlen > 4) {
                // Skip the 4-byte BE length prefix
                uint32_t clen = ((uint32_t)cd[0]<<24)|((uint32_t)cd[1]<<16)|((uint32_t)cd[2]<<8)|cd[3];
                const uint8_t* cdata = cd + 4;
                size_t reallen = clen < (cdlen-4) ? clen : (cdlen-4);
                for (size_t i = 0; i < reallen && i < 32; ++i)
                    snprintf(hex + i*3, 4, "%02x ", cdata[i]);
                utils::MainLogger::get().warn("Malformed BatchPacket from " + ip +
                    " (compressedLen=" + std::to_string(clen) + " firstBytes: " + hex + ")");
            } else {
                utils::MainLogger::get().warn("Malformed BatchPacket from " + ip + " (too short)");
            }
            if (auto* r = getRakLib()) r->closeSession(ip, port, "bad batch");
            return;
        }
        const auto& payload = bp.payload();
        // BATCH debug logging disabled.
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
            // Sub-packet debug logging disabled.
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

    // Lock the session's weak_ptr to get a safe shared_ptr. This keeps
    // the Player alive for the duration of this function call, even if
    // the main thread reaps it from players_ concurrently.
    std::shared_ptr<Player> player_sp;
    auto it = sessions_.find(sessionKey(ip, port));
    if (it != sessions_.end()) {
        player_sp = it->second.lock();
    }

    if (!player_sp) {
        // No session yet. Only LoginPacket may create a new session.
        if (pid != protocol::LOGIN_PACKET) {
            return;
        }
        if (!createPlayerForSession(ip, port)) return;
        // Re-lock the newly-registered session.
        auto it2 = sessions_.find(sessionKey(ip, port));
        if (it2 == sessions_.end()) return;
        player_sp = it2->second.lock();
        if (!player_sp) return;
    }

    // Decode the body (everything after the leading ID byte) via the
    // PacketPool registry and forward to the Player's dispatcher.
    auto pkt = decodePacketById(pid, data + 1, len - 1,
                                ip + ":" + std::to_string(port));
    if (!pkt) return;
    // CRITICAL: skip if the player was closed between the lock() above
    // and here. connected_ is atomic; if false, the player is being
    // torn down and we must NOT call onUpdate or handle packets.
    if (!player_sp->isConnected()) return;
    player_sp->handleDataPacket(*pkt);
}

std::shared_ptr<protocol::DataPacket> Network::decodePacketById(uint8_t pid,
        const uint8_t* body, size_t bodyLen, const std::string& who) {
    // Helper local: primeros N bytes en hex, para diagnóstico.
    auto hexDump = [](const uint8_t* b, size_t n) {
        size_t cap = n < 40 ? n : 40;
        std::string out;
        char tmp[4];
        for (size_t i = 0; i < cap; ++i) {
            snprintf(tmp, sizeof(tmp), "%02x ", b[i]);
            out += tmp;
        }
        return out;
    };

    // Instantiate the right subclass via the PacketPool registry.
    auto pkt = protocol::PacketPool::get().create(pid);
    if (!pkt) {
        // DIAGNÓSTICO (bug "tocar un item del creativo no llega al
        // inventario"): antes esto se ignoraba en silencio TOTAL —
        // ni un log — así que si el cliente real manda un tipo de
        // paquete que este server no tiene registrado en
        // PacketPool::create() (por ejemplo InventoryTransactionPacket
        // o CraftingEventPacket, ninguno de los dos existe hoy acá),
        // no había NINGUNA forma de saberlo. Gateado en advancedLog()
        // para no ensuciar el log en uso normal.
        char pidHex[4];
        snprintf(pidHex, sizeof(pidHex), "%02x", pid);
        const std::string detail = "len=" + std::to_string(bodyLen) + " bytes=" + hexDump(body, bodyLen);
        if (server_ && server_->advancedLog()) {
            utils::MainLogger::get().warn(std::string("[PKT] ID desconocido 0x") + pidHex +
                " " + detail);
        }
        // Always-on (no /workbench needed): record unknown packet IDs so a
        // later crash/error report can reveal what the client sent.
        diagnostics::Diagnostics::get().report("packet",
            std::string("unknown packet id 0x") + pidHex, detail,
            /*console=*/false);
        return nullptr;
    }
    bool ok = pkt->decode(body, bodyLen);
    if (!ok) {
        // Decode failed — record it ALWAYS (a malformed packet is exactly the
        // kind of thing that must show up in the crashdump).
        char pidHex[4];
        snprintf(pidHex, sizeof(pidHex), "%02x", pid);
        const std::string detail = "len=" + std::to_string(bodyLen) + " bytes=" + hexDump(body, bodyLen);
        diagnostics::Diagnostics::get().report("packet",
            std::string("decode() failed for id 0x") + pidHex, detail);
    }
    // DIAGNÓSTICO extra específico para CONTAINER_SET_SLOT_PACKET
    // (0x2c) — el sospechoso del bug de creativo. Se loguea SIEMPRE
    // que llegue uno (éxito o fallo de decode), para poder comparar
    // los bytes reales que manda el cliente contra el formato que
    // este server asume (ver el comentario de formato en
    // ContainerSetSlotPacket.h). Gateado en advancedLog().
    if (pid == protocol::CONTAINER_SET_SLOT_PACKET && server_ && server_->advancedLog()) {
        utils::MainLogger::get().info(std::string("[PKT] CONTAINER_SET_SLOT_PACKET decode=") +
            (ok ? "OK" : "FALLO") + " len=" + std::to_string(bodyLen) +
            " bytes=" + hexDump(body, bodyLen));
    }
    if (!ok) {
        // A LoginPacket that cannot be decoded is the one failure that
        // leaves the operator with NO clue: no Player is ever created, so
        // no "Closing player ..." line is printed either and the only
        // thing left in the log is "Session opened" + "Session closed" —
        // exactly the "no session was created" report. Always log the
        // concrete reason (short buffer / bad outerLen / zlib stream).
        if (pid == protocol::LOGIN_PACKET) {
            auto* lp = dynamic_cast<protocol::LoginPacket*>(pkt.get());
            utils::MainLogger::get().warn(
                "LoginPacket from " + who + " could not be decoded (len=" +
                std::to_string(bodyLen) + "): " +
                (!lp || lp->decode_error.empty() ? std::string("unknown")
                                                  : lp->decode_error) +
                " — no session created");
        }
        // Decode failed — silently drop (log above already covered it
        // when advancedLog() is on).
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
    // Allocate a fresh unique runtime entity ID for this player (PMMP uses
    // Entity::$entityCount for the same purpose). Without this, every player
    // shares EID 0 and AddPlayerPacket broadcasts confuse the client.
    cfg.runtime_eid     = server_->allocateEid();
    auto p = std::make_shared<Player>(server_, std::move(cfg));
    if (auto* r = getRakLib()) p->setSourceInterface(r);
    server_->addPlayer(p);
    // Store as weak_ptr so the Network doesn't keep the Player alive
    // after Server::tick() reaps it from players_.
    registerSession(ip, port, p);
    return true;
}

Player* Network::getPlayer(const std::string& ip, uint16_t port) {
    auto it = sessions_.find(sessionKey(ip, port));
    if (it == sessions_.end()) return nullptr;
    // Lock the weak_ptr; if the Player was destroyed, return nullptr.
    auto sp = it->second.lock();
    return sp.get();
}

void Network::registerSession(const std::string& ip, uint16_t port,
                                std::shared_ptr<Player> p) {
    sessions_[sessionKey(ip, port)] = p;  // weak_ptr from shared_ptr
}

void Network::unregisterSession(Player* p) {
    if (!p) return;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        auto sp = it->second.lock();
        if (sp.get() == p) { sessions_.erase(it); return; }
    }
}

void Network::addPlayer(std::unique_ptr<Player> /*p*/) {
    // Network doesn't own the player — Server::addPlayer does.
}

void Network::removePlayer(Player* p) {
    if (!p) return;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        auto sp = it->second.lock();
        if (sp.get() == p) { sessions_.erase(it); return; }
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
} // namespace cursestone
