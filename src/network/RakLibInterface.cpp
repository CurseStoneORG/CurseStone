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

namespace cursestone {
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
    std::string motd_str = "CurseStone";
    int         max_players = 20;
    int         online     = 0;
    if (server_) {
        host_str    = server_->ip();
        port        = server_->port();
        motd_str    = server_->getMotd();
        max_players = server_->maxPlayers();
        online      = (int)server_->playersSnapshot().size();
    }
    // FIX ("stuck on Sincronizando paquetes" on a hosted Linux box): 1492
    // only fits a path whose MTU is the full 1500 of plain Ethernet. A
    // VPS/VM/container/WSL2/tunnel typically has 1400 (VXLAN/overlay),
    // 1392 (some cloud fabrics) or 1350 (WSL2) — and the datagrams are
    // sent with the don't-fragment bit, so they are dropped, not split.
    // 1400 is the IPv6 minimum MTU and the conservative value RakNet
    // itself falls back to; it costs one extra datagram per ~20 fragments
    // (an 83KB chunk goes 57 -> 60 fragments) and works on every path.
    // This value is the max IP PACKET size; Session.c subtracts the IPv4
    // and UDP headers before sizing any payload.
    const uint16_t mtu = 1400;

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
    // FIX #4 (quantified per audit priority 7 — not an arbitrary number):
    // MTU is 1400 bytes (see the comment on the `mtu` constant below),
    // giving ~1360 usable bytes per fragment after the IPv4 + UDP +
    // RakNet datagram + encapsulation header overhead.
    // A serialized 0.15-format chunk (16 sections of block+data+skylight+
    // blocklight) is roughly 80-90KB raw; after the zlib compression this
    // codebase applies to every BatchPacket, NOTE this is an ESTIMATE, not
    // a measurement taken from an actual running server — real compressed
    // size varies widely with terrain repetition, anywhere from a few KB
    // (flat/repetitive terrain) up to 20-40KB (varied terrain, less
    // compressible). Call it roughly 2-30 fragments per chunk depending
    // on terrain. At the server's actual chunk-streaming rate of 1
    // chunk/tick (confirmed directly in Player.cpp — MAX_CHUNKS_PER_TICK
    // = 1; an earlier comment elsewhere in this codebase claiming 4/tick
    // does not match what the code currently does) and 20 TPS, that's
    // roughly 40-600 chunk-fragment datagrams/second in the estimated
    // range — well under what 64 sends/loop-iteration (at ~100Hz, since
    // this loop sleeps 10ms) can drain: 64 * 100 = 6400 sends/second
    // capacity leaves comfortable headroom even at the high end of the
    // estimate, while still capping how long any single loop iteration
    // can spend sending before yielding back to ACK/NACK/retransmit
    // processing — the actual failure mode this guards against.
    static constexpr size_t MAX_SENDS_PER_LOOP = 64;

    // --- Core debug: this loop is the heart of every player's connection,
    // so when two players are online and things go wrong we need to see
    // the loop's own health live, not 30 seconds later in a disconnect
    // dump. Three probes, all rate-limited to one line every 5s:
    //   * a tick that overruns badly (the thread is not keeping up, so
    //     datagrams pile up in the kernel and other players' packets get
    //     dropped -> they "stop responding" for no visible reason),
    //   * the outbound queue growing (the main thread is producing faster
    //     than RakLib can send — usually one client that cannot keep up),
    //   * how long the drain itself takes.
    uint64_t last_probe_ms = 0;
    constexpr uint64_t kProbeIntervalMs = 5000;
    constexpr uint64_t kSlowTickMs = 50;

    while (!stop_flag_.load(std::memory_order_relaxed)) {
        const auto tick_start = std::chrono::steady_clock::now();

        // 1. Drain up to MAX_SENDS_PER_LOOP outbound actions (main → RakLib)
        size_t queueDepth = 0, drainedTotal = 0;
        {
            std::lock_guard<std::mutex> lk(outbound_mtx_);
            queueDepth = outbound_queue_.size();
            size_t drained = 0;
            while (!outbound_queue_.empty() && drained < MAX_SENDS_PER_LOOP) {
                auto& action = outbound_queue_.front();
                uint8_t ipBytes[4];
                if (sscanf(action.ip.c_str(), "%hhu.%hhu.%hhu.%hhu",
                            &ipBytes[0], &ipBytes[1], &ipBytes[2], &ipBytes[3]) == 4) {
                    if (action.type == RakLibActionType::SEND_PACKET) {
                        // BUG FIX (v2): the previous fix here forced EVERY
                        // packet to RELIABLE (unordered) on channel 0 to
                        // avoid one lost reliable-ordered packet blocking
                        // all later ones on that channel. That avoided the
                        // deadlock, but it broke something else: PMMP/real
                        // MCPE servers rely on the login/spawn sequence
                        // (PlayStatus → StartGame → SetTime →
                        // SetSpawnPosition → SetDifficulty → SetHealth,
                        // each sent as its own BatchPacket here) arriving
                        // in that exact order. With everything on the same
                        // unordered channel, real network jitter/reorder —
                        // even mild lag — can deliver those out of order,
                        // and the real MCPE 0.15.10 client does not
                        // tolerate that: it silently breaks and eventually
                        // disconnects with "Disconnected from server",
                        // typically after a couple minutes of play once
                        // enough reordering has accumulated (chunk streams
                        // add many more unordered sends on top of the
                        // initial handshake, increasing the odds over
                        // time — matching the reported 3-5 minute pattern).
                        //
                        // Fix: use two separate channels instead of one.
                        //   - ordered=true  → RELIABLE_ORDERED on channel 0
                        //     (login/spawn packets only — few packets, so
                        //     a lost one and its retransmit-then-unblock
                        //     is fast and never observed by the player).
                        //   - ordered=false → RELIABLE on channel 1 (chunks,
                        //     entity updates, everything else) — same as
                        //     before, no ordering guarantee needed or given,
                        //     and it can never be blocked by channel 0.
                        // RakNet supports 32 independent ordering channels
                        // precisely so unrelated traffic doesn't have to
                        // share one queue — this restores that separation
                        // instead of collapsing everything onto channel 0.
                        uint8_t reliability   = action.ordered
                                                 ? MCP_RELIABILITY_RELIABLE_ORDERED
                                                 : MCP_RELIABILITY_RELIABLE;
                        uint8_t order_channel = action.ordered ? 0 : 1;
                        mcp_rak_session_manager_send_encapsulated(
                            &raklib_.manager, ipBytes, action.port,
                            action.data.data(), action.data.size(),
                            reliability, order_channel);
                    } else if (action.type == RakLibActionType::CLOSE_SESSION) {
                        mcp_server_handler_close_session(
                            &raklib_.handler, ipBytes, action.port);
                    }
                }
                outbound_queue_.pop();
                ++drained;
            }
            drainedTotal = drained;
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
        //    Running this every loop iteration — even mid-burst, after
        //    only a partial drain of outbound_queue_ — is exactly the
        //    point of the cap above.
        mcp_raklib_server_tick(&raklib_);

        // 5. Core health probe (see the comment above the loop).
        const uint64_t now_ms = mcp_utils_time_ms();
        if (now_ms - last_probe_ms >= kProbeIntervalMs) {
            const auto tick_ms = (uint64_t)std::chrono::duration_cast<
                std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tick_start).count();
            if (tick_ms >= kSlowTickMs || queueDepth >= MAX_SENDS_PER_LOOP) {
                char buf[256];
                snprintf(buf, sizeof buf,
                    "[RakLib][debug] tick=%llums outbound_queue=%zu "
                    "drained=%zu peak=%zu — %s",
                    (unsigned long long)tick_ms, queueDepth, drainedTotal,
                    outbound_queue_peak_.load(std::memory_order_relaxed),
                    queueDepth >= MAX_SENDS_PER_LOOP
                        ? "main thread is producing faster than RakLib sends"
                        : "network thread is behind, datagrams may be dropped");
                utils::MainLogger::get().warn(buf);
                last_probe_ms = now_ms;
            }
        }

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
                                     const std::vector<uint8_t>& data,
                                     bool ordered) {
    if (!running_.load()) return;
    RakLibAction action;
    action.type    = RakLibActionType::SEND_PACKET;
    action.ip      = ip;
    action.port    = port;
    action.data    = data;
    action.ordered = ordered;
    {
        std::lock_guard<std::mutex> lk(outbound_mtx_);
        outbound_queue_.push(std::move(action));
        size_t sz = outbound_queue_.size();
        size_t prev = outbound_queue_peak_.load(std::memory_order_relaxed);
        if (sz > prev) outbound_queue_peak_.store(sz, std::memory_order_relaxed);
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
        size_t sz = outbound_queue_.size();
        size_t prev = outbound_queue_peak_.load(std::memory_order_relaxed);
        if (sz > prev) outbound_queue_peak_.store(sz, std::memory_order_relaxed);
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
} // namespace cursestone
