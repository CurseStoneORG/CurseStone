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
// WorkbenchManager.h — comprehensive server diagnostics system.
//
// 6 modules:
//   1. TPS Monitor — tracks real TPS every second
//   2. Tick Profiler — times each phase of Server::tick()
//   3. Packet Logger — logs every IN/OUT packet
//   4. RakLib Stats — atomic counters for send/recv/retransmit
//   5. Network Watchdog — separate thread detecting main-thread stalls
//   6. Disconnect Crash Dump — captures state when a player disconnects
//
// Usage:
//   WorkbenchManager::get().start();   // via /workbench start
//   WorkbenchManager::get().stop();    // via /workbench stop → exports reports
//   WorkbenchManager::get().report();  // via /workbench report → snapshot export
//   WorkbenchManager::get().status();  // via /workbench status → console output
//
// Hook points (called from Server::tick):
//   WorkbenchManager::get().onTickStart();
//   WorkbenchManager::get().onTickPhase(phase_name, duration_ms);
//   WorkbenchManager::get().onTickEnd(total_ms);
//   WorkbenchManager::get().onPlayerUpdate(player_name, duration_ms);
//   WorkbenchManager::get().onPacketIn(ip, port, pid, size);
//   WorkbenchManager::get().onPacketOut(player_name, pid, size);
//   WorkbenchManager::get().onDisconnect(player_name, reason, session_ms);
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace cursestone {

// ---- Tick profiler sample ----
struct TickSample {
    uint64_t tick_id = 0;
    uint64_t total_ms = 0;
    uint64_t scheduler_ms = 0;
    uint64_t level_ms = 0;
    uint64_t network_ms = 0;
    uint64_t autosave_ms = 0;
    std::vector<std::pair<std::string, uint64_t>> player_ms;
};

// ---- Packet log entry ----
struct PacketLogEntry {
    std::string timestamp;
    std::string direction; // "IN " or "OUT"
    std::string player;
    uint8_t pid = 0;
    size_t size = 0;
};

// ---- Disconnect dump ----
struct DisconnectDump {
    std::string player_name;
    std::string reason;
    uint64_t session_duration_ms = 0;
    std::vector<PacketLogEntry> last_packets;
    std::vector<double> tps_last_5s;
    std::vector<uint64_t> tick_durations_last_5;
    uint64_t chunk_queue_pending = 0;
    uint64_t last_activity_delta_ms = 0;
    std::string diagnosis;
};

class WorkbenchManager {
  public:
    static WorkbenchManager& get() {
        static WorkbenchManager instance;
        return instance;
    }

    // ---- Lifecycle ----
    void start();
    void stop();
    void report(); // export without stopping
    void status(); // print to console

    bool isActive() const {
        return active_.load(std::memory_order_relaxed);
    }

    // ---- Hook points (called from Server::tick / Network / Player) ----
    void onTickStart();
    void onTickPhase(const char* phase_name, uint64_t duration_ms);
    void onTickEnd(uint64_t total_ms);
    void onPlayerUpdate(const std::string& name, uint64_t duration_ms);
    void onAutosave(uint64_t duration_ms);
    void onPacketIn(const std::string& ip_port, uint8_t pid, size_t size);
    void onPacketOut(const std::string& player, uint8_t pid, size_t size);
    void onDisconnect(const std::string& player, const std::string& reason, uint64_t session_ms,
                      uint64_t last_activity_delta_ms, size_t chunk_queue_pending);

    // ---- RakLib atomic counters (incremented from RakLib thread) ----
    std::atomic<uint64_t> raklib_pkts_sent{0};
    std::atomic<uint64_t> raklib_pkts_recv{0};
    std::atomic<uint64_t> raklib_retransmits{0};
    std::atomic<uint64_t> raklib_acks_sent{0};
    std::atomic<uint64_t> raklib_nacks_sent{0};
    std::atomic<uint64_t> raklib_pings_recv{0};
    std::atomic<uint64_t> raklib_game_pkts_recv{0};

    // ---- Watchdog: main thread updates this every network->tick() ----
    std::atomic<uint64_t> last_drain_time_ms{0};

  private:
    WorkbenchManager() = default;
    ~WorkbenchManager();

    // ---- Module 1: TPS Monitor ----
    void tpsMonitorTick();
    void exportTps(const std::string& dir);
    std::deque<uint64_t> tick_timestamps_; // last 20 tick times (ms)
    std::mutex tps_mtx_;
    std::deque<std::string> tps_log_;
    double tps_sum_ = 0;
    uint64_t tps_count_ = 0;
    double tps_min_ = 999.0;
    uint64_t tps_min_tick_ = 0;
    std::vector<std::pair<uint64_t, double>> tps_drops_; // ticks where TPS < 15

    // ---- Module 2: Tick Profiler ----
    void exportTickProfiler(const std::string& dir);
    std::deque<TickSample> slow_ticks_; // ring buffer, max 1000
    std::mutex profiler_mtx_;
    uint64_t total_ticks_monitored_ = 0;
    uint64_t current_tick_id_ = 0;
    // Current tick being built (cleared on onTickEnd)
    TickSample current_sample_;

    // ---- Module 3: Packet Logger ----
    void exportPackets(const std::string& dir);
    std::deque<PacketLogEntry> packet_log_;
    std::mutex packet_mtx_;
    static const size_t MAX_PACKET_LOG = 100000;

    // ---- Module 5: Watchdog thread ----
    void watchdogLoop();
    std::thread watchdog_thread_;
    std::atomic<bool> watchdog_stop_{false};
    std::deque<std::string> watchdog_log_;
    std::mutex watchdog_mtx_;
    uint64_t watchdog_stalls_200_ = 0;
    uint64_t watchdog_stalls_500_ = 0;
    uint64_t watchdog_worst_stall_ms_ = 0;
    std::string watchdog_worst_stall_time_;
    uint64_t watchdog_total_lag_ms_ = 0;
    uint64_t watchdog_samples_ = 0;

    // ---- Module 6: Disconnect dumps ----
    void exportDisconnects(const std::string& dir);
    std::vector<DisconnectDump> disconnects_;
    std::mutex disconnect_mtx_;

    // ---- Export helpers ----
    void exportAll(const std::string& dir);
    void exportSummary(const std::string& dir);
    std::string timestampStr() const;
    static std::string pidToName(uint8_t pid);
    static std::string formatDuration(uint64_t ms);

    std::atomic<bool> active_{false};
    uint64_t start_time_ms_ = 0;
};

} // namespace cursestone
