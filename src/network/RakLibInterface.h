// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLibInterface — C++ wrapper around the C raklib/RakLibServer.
//
// ARCHITECTURE: RakLib runs on its OWN THREAD (raklib_thread_) at
// ~100Hz (10ms sleep). It processes UDP recv/send, keepalives,
// ACKs/NACKs, retransmits and timeouts independently of the main
// tick loop.
//
// Two thread-safe FIFO queues bridge RakLib ↔ main thread:
//   inbound_queue_  (RakLib → main): received packets + session events
//   outbound_queue_ (main → RakLib): packets to send + session closes
//
// The main thread drains inbound_queue_ in Network::tick(), processes
// packets, then posts any responses to outbound_queue_. The RakLib
// thread drains outbound_queue_ before each mcp_raklib_server_tick().
//
// This mirrors PMMP's RakLibServer (which extends Thread) and
// communicates via ThreadSafeArray.
#pragma once

#include "AdvancedSourceInterface.h"
#include "raklib/RakLibServer.h"
#include "raklib/ServerInstance.h"
#include "raklib/SessionManager.h"
#include "../main.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace cursestone {
class Server;

namespace network {

// Inbound event types (RakLib → main thread)
enum class RakLibEventType {
    SESSION_OPEN,
    SESSION_CLOSE,
    PACKET_RECEIVED
};

struct RakLibEvent {
    RakLibEventType type;
    std::string     ip;
    uint16_t        port = 0;
    int64_t         clientId = 0;
    std::string     reason;
    std::vector<uint8_t> data;
};

// Outbound action types (main → RakLib)
enum class RakLibActionType {
    SEND_PACKET,
    CLOSE_SESSION
};

struct RakLibAction {
    RakLibActionType type;
    std::string      ip;
    uint16_t         port = 0;
    std::vector<uint8_t> data;
    std::string      reason;
    // BUG FIX: see raklibThreadLoop() for the full story. True routes
    // this send through RELIABLE_ORDERED on a dedicated channel so
    // strict-sequence packets (login/spawn) can never arrive out of
    // order; false (default) keeps the old RELIABLE-unordered
    // behavior for everything else (chunks, entity updates, ...),
    // now on its own separate channel so it can never block ordered
    // traffic either.
    bool             ordered = false;
};

class RakLibInterface : public AdvancedSourceInterface {
public:
    explicit RakLibInterface(Server* server);
    ~RakLibInterface() override;

    std::string getName() const override { return "raklib"; }
    void start() override;
    void shutdown() override;
    void tick() override;  // Drains inbound_queue_ on main thread

    // AdvancedSourceInterface — these now enqueue to outbound_queue_
    // ordered=true sends this packet RELIABLE_ORDERED on a dedicated
    // channel, guaranteeing it's processed by the client in the exact
    // order it was sent relative to other ordered=true sends. Use for
    // strict-sequence packets (login/spawn handshake). Default false
    // keeps the existing unordered RELIABLE behavior.
    void sendRawPacket(const std::string& ip, uint16_t port,
                         const std::vector<uint8_t>& data,
                         bool ordered = false) override;
    void closeSession(const std::string& ip, uint16_t port,
                        const std::string& reason) override;

    void setServerName(const std::string& motd);

    // C-to-C++ callbacks (called from RakLib thread)
    void onOpenSession(const std::string& ip, uint16_t port, int64_t clientId);
    void onCloseSession(const std::string& ip, uint16_t port,
                          const std::string& reason);
    void onPacketReceive(const std::string& ip, uint16_t port,
                          const uint8_t* data, size_t len);
    void onRawPacket(const std::string& ip, uint16_t port,
                       const uint8_t* data, size_t len);
    void onTick();

    int64_t getServerId() const { return serverId_; }
    bool    isRunning()   const { return running_; }

    // Drain pending events (called by Network::tick on main thread).
    // Returns the number of events processed.
    size_t drainEvents();

private:
    static void c_onOpen  (void* impl, const char* ip, uint16_t port, int64_t cid);
    static void c_onClose (void* impl, const char* ip, uint16_t port, const char* reason);
    static void c_onPacket(void* impl, const char* ip, uint16_t port,
                            const uint8_t* data, size_t len);
    static void c_onRaw   (void* impl, const char* ip, uint16_t port,
                            const uint8_t* data, size_t len);
    static void c_onTick  (void* impl);

    // RakLib thread loop — runs at ~100Hz
    void raklibThreadLoop();

    Server*               server_;
    mcp_raklib_server_t   raklib_;
    mcp_server_instance_t instance_;
    int64_t               serverId_ = 0;
    std::atomic<bool>     running_{false};
    std::atomic<bool>     stop_flag_{false};

    // RakLib worker thread
    std::thread           raklib_thread_;

    // Inbound queue (RakLib → main): events to process on main thread
    std::queue<RakLibEvent> inbound_queue_;
    std::mutex             inbound_mtx_;

    // Outbound queue (main → RakLib): packets to send / sessions to close
    std::queue<RakLibAction> outbound_queue_;
    std::mutex               outbound_mtx_;
    // FIX (audit priority 12/13): peak observed outbound_queue_ size —
    // diagnostic only, read by getOutboundQueuePeak() so it can be
    // surfaced if this session's disconnect needs correlating with
    // outbound backlog. Updated under outbound_mtx_ alongside the queue
    // itself, so no separate synchronization is needed for it.
    std::atomic<size_t>     outbound_queue_peak_{0};

public:
    size_t getOutboundQueuePeak() const {
        return outbound_queue_peak_.load(std::memory_order_relaxed);
    }
private:

    // Motd update flag (main → RakLib)
    std::mutex             motd_mtx_;
    std::string            pending_motd_;
    std::atomic<bool>      motd_changed_{false};
};

} // namespace network
} // namespace cursestone
