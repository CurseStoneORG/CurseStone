// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLibInterface.cpp — C++ bridge: RakLib C ↔ Server
// Handles the full MCPE 0.15.10 / Protocol 84 lifecycle.
#include "RakLibInterface.h"
#include "Network.h"
#include "../Server.h"
#include "../utils/Utils.h"

#include <sstream>
#include <cstring>
#include <iostream>
#include <cstdio>

namespace mcppeserver {
namespace network {

RakLibInterface::RakLibInterface(Server* server)
    : server_(server) {
    serverId_ = static_cast<int64_t>(mcp_utils_time_ms()) ^
                static_cast<int64_t>(reinterpret_cast<uintptr_t>(this));
    instance_.impl              = this;
    instance_.on_open_session   = &RakLibInterface::c_onOpen;
    instance_.on_close_session  = &RakLibInterface::c_onClose;
    instance_.on_packet_receive = &RakLibInterface::c_onPacket;
    instance_.on_raw_packet     = &RakLibInterface::c_onRaw;
    instance_.on_tick           = &RakLibInterface::c_onTick;
}

RakLibInterface::~RakLibInterface() {
    shutdown();
}

void RakLibInterface::start() {
    if (running_) return;

    // Read config from server (host / port / mtu / motd)
    const char* host = "0.0.0.0";
    uint16_t    port = 19132;
    uint16_t    mtu  = 1492;

    mcp_raklib_server_init(&raklib_, serverId_, mtu, &instance_);

    // Set server MOTD / name for UNCONNECTED_PONG
    // Format MCPE uses: "MCCPP;Demo;84;0.15.10;<online>;<max>"
    // We build it here; Server can update via setServerName().
    char motd[256];
    snprintf(motd, sizeof(motd),
             "MCCPP;Demo;84;0.15.10;%d;%d;%lld;%s;Survival",
             0,   // online count (TODO: wire to Server::getOnlinePlayerCount)
             20,  // max players  (TODO: wire to Server config)
             (long long)serverId_,
             "MCPPEServer");
    mcp_rak_session_manager_set_name(&raklib_.manager, motd);

    if (mcp_raklib_server_start(&raklib_, host, port) != 0) {
        std::cerr << "[RakLib] Failed to bind " << host << ":" << port << "\n";
        return;
    }
    running_ = true;
    std::cout << "[RakLib] Listening on " << host << ":" << port
              << " (Protocol 84 / MCPE 0.15.10)\n";
}

void RakLibInterface::shutdown() {
    if (!running_) return;
    mcp_raklib_server_stop(&raklib_);
    running_ = false;
}

void RakLibInterface::tick() {
    if (!running_) return;
    mcp_raklib_server_tick(&raklib_);
}

// Called by Server when the player count or MOTD changes.
void RakLibInterface::setServerName(const std::string& motd) {
    if (!running_) return;
    mcp_rak_session_manager_set_name(&raklib_.manager, motd.c_str());
}

// Send an encapsulated MCPE game packet to an established client.
void RakLibInterface::sendRawPacket(const std::string& ip, uint16_t port,
                                     const std::vector<uint8_t>& data) {
    if (!running_) return;
    uint8_t ipBytes[4];
    if (sscanf(ip.c_str(), "%hhu.%hhu.%hhu.%hhu",
                &ipBytes[0], &ipBytes[1], &ipBytes[2], &ipBytes[3]) != 4) return;
    mcp_rak_session_manager_send_encapsulated(&raklib_.manager,
        ipBytes, port, data.data(), data.size(),
        MCP_RELIABILITY_RELIABLE_ORDERED);
}

// Force-close a session (kick).
void RakLibInterface::closeSession(const std::string& ip, uint16_t port,
                                    const std::string& reason) {
    (void)reason;
    if (!running_) return;
    uint8_t ipBytes[4];
    if (sscanf(ip.c_str(), "%hhu.%hhu.%hhu.%hhu",
                &ipBytes[0], &ipBytes[1], &ipBytes[2], &ipBytes[3]) != 4) return;
    mcp_server_handler_close_session(&raklib_.handler, ipBytes, port);
}

// ---- C++ event handlers ------------------------------------------------

void RakLibInterface::onOpenSession(const std::string& ip, uint16_t port,
                                     int64_t clientId) {
    std::cout << "[RakLib] Session opened: " << ip << ":" << port
              << " (clientId=" << clientId << ")\n";
    if (server_ && server_->getNetwork()) {
        server_->getNetwork()->onSessionOpen(ip, port, clientId);
    }
}

void RakLibInterface::onCloseSession(const std::string& ip, uint16_t port,
                                      const std::string& reason) {
    std::cout << "[RakLib] Session closed: " << ip << ":" << port
              << " (" << reason << ")\n";
    if (server_ && server_->getNetwork()) {
        server_->getNetwork()->onSessionClose(ip, port, reason);
    }
}

void RakLibInterface::onPacketReceive(const std::string& ip, uint16_t port,
                                       const uint8_t* data, size_t len) {
    // Game packet received from a fully connected client.
    // The first byte is the MCPE packet ID. Forward to Network, which
    // will decode BatchPacket, dispatch sub-packets to the right Player.
    if (server_ && server_->getNetwork()) {
        server_->getNetwork()->processIncoming(ip, port, data, len);
    }
}

void RakLibInterface::onRawPacket(const std::string& ip, uint16_t port,
                                   const uint8_t* data, size_t len) {
    // Unhandled offline packets (e.g. query); ignore for now.
    (void)ip; (void)port; (void)data; (void)len;
}

void RakLibInterface::onTick() {
    // No-op — tick is driven from RakLibInterface::tick().
}

// ---- Static C shims ----------------------------------------------------

void RakLibInterface::c_onOpen(void* impl, const char* ip, uint16_t port,
                                int64_t cid) {
    static_cast<RakLibInterface*>(impl)->onOpenSession(ip, port, cid);
}
void RakLibInterface::c_onClose(void* impl, const char* ip, uint16_t port,
                                 const char* reason) {
    static_cast<RakLibInterface*>(impl)->onCloseSession(
        ip, port, reason ? reason : "");
}
void RakLibInterface::c_onPacket(void* impl, const char* ip, uint16_t port,
                                  const uint8_t* data, size_t len) {
    static_cast<RakLibInterface*>(impl)->onPacketReceive(ip, port, data, len);
}
void RakLibInterface::c_onRaw(void* impl, const char* ip, uint16_t port,
                               const uint8_t* data, size_t len) {
    static_cast<RakLibInterface*>(impl)->onRawPacket(ip, port, data, len);
}
void RakLibInterface::c_onTick(void* impl) {
    static_cast<RakLibInterface*>(impl)->onTick();
}

} // namespace network
} // namespace mcppeserver
