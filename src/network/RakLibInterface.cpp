// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLibInterface.cpp — C++ bridge: RakLib C ↔ Server
//
// RakLib runs on its OWN THREAD. Two FIFO queues bridge it to the
// main thread. This prevents chunk generation from blocking
// keepalive processing.
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
#include <chrono>

namespace atomikpe {
namespace network {

// ============================================================================
// Constructor / Destructor
// ============================================================================
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

// ============================================================================
// start() — init RakLib + spawn the worker thread
// ============================================================================
void RakLibInterface::start() {
    if (running_.load()) return;

    std::string host_str = "0.0.0.0";
    uint16_t    port     = 19132;
    std::string motd_str = "AtomikPE";
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
    running_.store(true);
    stop_flag_.store(false);

    // Spawn the RakLib worker thread — runs at ~100Hz (10ms sleep)
    raklib_thread_ = std::thread([this]() { this->raklibThreadLoop(); });

    utils::MainLogger::get().info(
        std::string("[RakLib] Listening on ") + host_str + ":" + std::to_string(port) +
        " (Protocol 84 / MCPE 0.15.10) [separate thread]");
    utils::MainLogger::get().info(std::string("[RakLib] MOTD: ") + motd);
}

// ============================================================================
// shutdown() — stop thread + cleanup
// ============================================================================
void RakLibInterface::shutdown() {
    if (!running_.load()) return;
    // Fix #2: set stop_flag FIRST, then running_=false. This prevents
    // the RakLib thread from entering another tick cycle between the
    // two stores.
    stop_flag_.store(true);
    running_.store(false);

    // Wait for the RakLib thread to exit
    if (raklib_thread_.joinable()) {
        raklib_thread_.join();
    }

    mcp_raklib_server_stop(&raklib_);
}

// ============================================================================
// raklibThreadLoop() — runs on the RakLib worker thread
// ============================================================================
void RakLibInterface::raklibThreadLoop() {
    while (!stop_flag_.load(std::memory_order_relaxed)) {
        // 1. Drain outbound queue (main → RakLib)
        {
            std::lock_guard<std::mutex> lk(outbound_mtx_);
            while (!outbound_queue_.empty()) {
                auto& action = outbound_queue_.front();
                uint8_t ipBytes[4];
                if (sscanf(action.ip.c_str(), "%hhu.%hhu.%hhu.%hhu",
                            &ipBytes[0], &ipBytes[1], &ipBytes[2], &ipBytes[3]) == 4) {
                    if (action.type == RakLibActionType::SEND_PACKET) {
                        // BUG FIX: Use RELIABLE (2) instead of RELIABLE_ORDERED (3)
                        // for ALL packets. The MCPE 0.15 client has a single
                        // ordering channel (0) and if ANY reliable-ordered packet
                        // is lost, ALL subsequent packets on that channel are
                        // blocked until retransmit. With chunk data being sent
                        // as separate BatchPackets, a single lost fragment
                        // deadlocks the entire ordering queue → client stops
                        // responding → "Disconnected from server" timeout.
                        //
                        // PMMP uses RELIABLE_ORDERED but batches ALL packets
                        // per tick into ONE BatchPacket, so there's only one
                        // orderIndex per tick. AtomikPE sends each packet as
                        // a separate BatchPacket, so we must use RELIABLE to
                        // avoid ordering deadlocks.
                        //
                        // Chunks, block updates, and entity updates don't
                        // actually need ordering — the client can process
                        // them in any order. Only login/spawn sequence needs
                        // strict ordering, and those are sent one-at-a-time
                        // anyway.
                        mcp_rak_session_manager_send_encapsulated(
                            &raklib_.manager, ipBytes, action.port,
                            action.data.data(), action.data.size(),
                            MCP_RELIABILITY_RELIABLE);
                    } else if (action.type == RakLibActionType::CLOSE_SESSION) {
                        mcp_server_handler_close_session(
                            &raklib_.handler, ipBytes, action.port);
                    }
                }
                outbound_queue_.pop();
            }
        }

        // 2. Check for MOTD update
        if (motd_changed_.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(motd_mtx_);
            mcp_rak_session_manager_set_name(&raklib_.manager, pending_motd_.c_str());
            pending_motd_.clear();
            motd_changed_.store(false, std::memory_order_relaxed);
        }

        // 3. Tick RakLib — this processes UDP recv/send, keepalives,
        //    ACK/NACK, retransmit, timeout sweep. All on THIS thread.
        mcp_raklib_server_tick(&raklib_);

        // 4. Sleep 10ms (~100Hz) — independent of main tick loop
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ============================================================================
// tick() — called by Network::tick() on the MAIN thread
// Drains the inbound queue (RakLib → main)
// ============================================================================
void RakLibInterface::tick() {
    drainEvents();
}

size_t RakLibInterface::drainEvents() {
    size_t count = 0;
    // Process all pending events from RakLib
    while (true) {
        RakLibEvent event;
        {
            std::lock_guard<std::mutex> lk(inbound_mtx_);
            if (inbound_queue_.empty()) break;
            event = std::move(inbound_queue_.front());
            inbound_queue_.pop();
        }
        count++;

        switch (event.type) {
            case RakLibEventType::SESSION_OPEN: {
                utils::MainLogger::get().info("[RakLib] Session opened: " + event.ip + ":" +
                    std::to_string(event.port) + " (clientId=" +
                    std::to_string(event.clientId) + ")");
                if (server_ && server_->getNetwork()) {
                    server_->getNetwork()->onSessionOpen(event.ip, event.port, event.clientId);
                }
                break;
            }
            case RakLibEventType::SESSION_CLOSE: {
                utils::MainLogger::get().info("[RakLib] Session closed: " + event.ip + ":" +
                    std::to_string(event.port) + " (" + event.reason + ")");
                if (server_ && server_->getNetwork()) {
                    server_->getNetwork()->onSessionClose(event.ip, event.port, event.reason);
                }
                break;
            }
            case RakLibEventType::PACKET_RECEIVED: {
                if (server_ && server_->getNetwork()) {
                    server_->getNetwork()->processIncoming(
                        event.ip, event.port, event.data.data(), event.data.size());
                }
                break;
            }
        }
    }
    return count;
}

// ============================================================================
// setServerName() — enqueue MOTD update for RakLib thread
// ============================================================================
void RakLibInterface::setServerName(const std::string& motd) {
    if (!running_.load()) return;
    {
        std::lock_guard<std::mutex> lk(motd_mtx_);
        pending_motd_ = motd;
    }
    motd_changed_.store(true, std::memory_order_relaxed);
}

// ============================================================================
// sendRawPacket() — enqueue to outbound queue (main → RakLib)
// ============================================================================
void RakLibInterface::sendRawPacket(const std::string& ip, uint16_t port,
                                     const std::vector<uint8_t>& data) {
    if (!running_.load()) return;
    RakLibAction action;
    action.type = RakLibActionType::SEND_PACKET;
    action.ip   = ip;
    action.port = port;
    action.data = data;
    {
        std::lock_guard<std::mutex> lk(outbound_mtx_);
        outbound_queue_.push(std::move(action));
    }
}

// ============================================================================
// closeSession() — enqueue close to outbound queue (main → RakLib)
// ============================================================================
void RakLibInterface::closeSession(const std::string& ip, uint16_t port,
                                    const std::string& reason) {
    (void)reason;
    if (!running_.load()) return;
    RakLibAction action;
    action.type = RakLibActionType::CLOSE_SESSION;
    action.ip   = ip;
    action.port = port;
    {
        std::lock_guard<std::mutex> lk(outbound_mtx_);
        outbound_queue_.push(std::move(action));
    }
}

// ============================================================================
// C++ event handlers — called from RAKLIB THREAD, enqueue to inbound
// ============================================================================
void RakLibInterface::onOpenSession(const std::string& ip, uint16_t port,
                                     int64_t clientId) {
    RakLibEvent event;
    event.type     = RakLibEventType::SESSION_OPEN;
    event.ip       = ip;
    event.port     = port;
    event.clientId = clientId;
    {
        std::lock_guard<std::mutex> lk(inbound_mtx_);
        inbound_queue_.push(std::move(event));
    }
}

void RakLibInterface::onCloseSession(const std::string& ip, uint16_t port,
                                      const std::string& reason) {
    RakLibEvent event;
    event.type   = RakLibEventType::SESSION_CLOSE;
    event.ip     = ip;
    event.port   = port;
    event.reason = reason;
    {
        std::lock_guard<std::mutex> lk(inbound_mtx_);
        inbound_queue_.push(std::move(event));
    }
}

void RakLibInterface::onPacketReceive(const std::string& ip, uint16_t port,
                                       const uint8_t* data, size_t len) {
    RakLibEvent event;
    event.type = RakLibEventType::PACKET_RECEIVED;
    event.ip   = ip;
    event.port = port;
    event.data.assign(data, data + len);
    {
        std::lock_guard<std::mutex> lk(inbound_mtx_);
        inbound_queue_.push(std::move(event));
    }
}

void RakLibInterface::onRawPacket(const std::string& ip, uint16_t port,
                                   const uint8_t* data, size_t len) {
    (void)ip; (void)port; (void)data; (void)len;
}

void RakLibInterface::onTick() {
    // No-op — the RakLib thread handles its own ticking.
}

// ============================================================================
// Static C shims
// ============================================================================
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
} // namespace atomikpe
