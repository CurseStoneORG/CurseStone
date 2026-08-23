// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLibInterface.cpp — C++ bridge: RakLib C ↔ Server
// Handles the full MCPE 0.15.10 / Protocol 84 lifecycle.
#include "RakLibInterface.h"
#include "Network.h"
#include "../Server.h"
#include "../utils/Utils.h"
#include "../utils/MainLogger.hpp"

#include <sstream>
#include <cstring>
#include <iostream>
#include <cstdio>
#include <string>

namespace cenisyspe {
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

    // Read host/port/motd/max-players from the Server config so the user's
    // cenisyspe.yml choices actually take effect. Falls back to sensible
    // defaults if Server is null (shouldn't happen, but defensive).
    std::string host_str = "0.0.0.0";
    uint16_t    port     = 19132;
    std::string motd_str = "CenisysPE";
    int         max_players = 20;
    int         online     = 0;
    if (server_) {
        host_str    = server_->ip();
        port        = server_->port();
        motd_str    = server_->getMotd();
        max_players = server_->maxPlayers();
        online      = (int)server_->playersSnapshot().size();
    }
    uint16_t mtu  = 1492;

    mcp_raklib_server_init(&raklib_, serverId_, mtu, &instance_);

    // Set server MOTD / name for UNCONNECTED_PONG.
    // MCPE 0.15.10 (protocol 84) expects exactly 6 semicolon-separated fields,
    // matching PMMP RakLibInterface.php:
    //   MCPE;<motd>;<protocol>;<version>;<online>;<max>
    char motd[512];
    snprintf(motd, sizeof(motd),
             "MCPE;%s;84;0.15.10;%d;%d",
             motd_str.c_str(), online, max_players);
    mcp_rak_session_manager_set_name(&raklib_.manager, motd);

    if (mcp_raklib_server_start(&raklib_, host_str.c_str(), port) != 0) {
        utils::MainLogger::get().error(
            std::string("[RakLib] Failed to bind ") + host_str + ":" + std::to_string(port));
        return;
    }
    running_ = true;
    utils::MainLogger::get().info(
        std::string("[RakLib] Listening on ") + host_str + ":" + std::to_string(port) +
        " (Protocol 84 / MCPE 0.15.10)");
    utils::MainLogger::get().info(std::string("[RakLib] MOTD: ") + motd);
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
    utils::MainLogger::get().info("[RakLib] Session opened: " + ip + ":" +
        std::to_string(port) + " (clientId=" + std::to_string(clientId) + ")");
    if (server_ && server_->getNetwork()) {
        server_->getNetwork()->onSessionOpen(ip, port, clientId);
    }
}

void RakLibInterface::onCloseSession(const std::string& ip, uint16_t port,
                                      const std::string& reason) {
    utils::MainLogger::get().info("[RakLib] Session closed: " + ip + ":" +
        std::to_string(port) + " (" + reason + ")");
    if (server_ && server_->getNetwork()) {
        server_->getNetwork()->onSessionClose(ip, port, reason);
    }
}

void RakLibInterface::onPacketReceive(const std::string& ip, uint16_t port,
                                       const uint8_t* data, size_t len) {
    // Game packet received from a fully connected client.
    // MCPE clients prepend 0xFE to all game packets. Network::processIncoming
    // will strip it and decode the BATCH/LoginPacket inside.
    if (server_ && server_->advancedLog() && len > 0) {
        char hex[8];
        snprintf(hex, sizeof(hex), "%02x", data[0]);
        utils::MainLogger::get().info("[RakLib] Packet from " + ip + ":" +
            std::to_string(port) + " id=0x" + hex + " len=" + std::to_string(len));
    }
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
} // namespace cenisyspe
