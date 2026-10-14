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
// WorkbenchManager.cpp — implementation of all 6 diagnostic modules.
#include "WorkbenchManager.h"
#include "utils/MainLogger.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include "utils/PlatformCompat.h"
#include <iomanip>

namespace cursestone {

static uint64_t now_ms() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string time_str() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03u", buf, (unsigned)ms.count());
    return out;
}

std::string WorkbenchManager::timestampStr() const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return buf;
}

std::string WorkbenchManager::formatDuration(uint64_t ms) {
    uint64_t s = ms / 1000;
    uint64_t h = s / 3600;
    s %= 3600;
    uint64_t m = s / 60;
    s %= 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu",
                   (unsigned long long)h, (unsigned long long)m, (unsigned long long)s);
    return buf;
}

std::string WorkbenchManager::pidToName(uint8_t pid) {
    switch (pid) {
        case 0x00: return "PING";
        case 0x01: return "LoginPacket";
        case 0x02: return "PlayStatusPacket";
        case 0x05: return "DisconnectPacket";
        case 0x06: return "BatchPacket";
        case 0x07: return "TextPacket";
        case 0x08: return "SetTimePacket";
        case 0x09: return "StartGamePacket";
        case 0x0a: return "AddPlayerPacket";
        case 0x0c: return "RemoveEntityPacket";
        case 0x0d: return "AddItemEntityPacket";
        case 0x10: return "MovePlayerPacket";
        case 0x12: return "RemoveBlockPacket";
        case 0x13: return "UpdateBlockPacket";
        case 0x16: return "LevelEventPacket";
        case 0x1b: return "MobEquipmentPacket";
        case 0x1c: return "MobArmorEquipmentPacket";
        case 0x1f: return "UseItemPacket";
        case 0x20: return "PlayerActionPacket";
        case 0x27: return "AnimatePacket";
        case 0x2c: return "ContainerSetSlotPacket";
        case 0x2e: return "ContainerSetContentPacket";
        case 0x34: return "FullChunkDataPacket";
        case 0x38: return "PlayerListPacket";
        case 0x3d: return "RequestChunkRadiusPacket";
        case 0x3e: return "ChunkRadiusUpdatedPacket";
        case 0xFE: return "GamePacket";
        default:   return "Unknown(0x" + ([]() { return std::string(); }()) +
                           std::to_string(pid) + ")";
    }
}

// ============================================================
// Lifecycle
// ============================================================

void WorkbenchManager::start() {
    if (active_.load(std::memory_order_relaxed)) {
        utils::MainLogger::get().info("[WORKBENCH] Already active.");
        return;
    }
    active_.store(true, std::memory_order_relaxed);
    start_time_ms_ = now_ms();

    // Reset all state
    {
        std::lock_guard<std::mutex> lk(tps_mtx_);
        tick_timestamps_.clear();
        tps_log_.clear();
        tps_sum_ = 0; tps_count_ = 0;
        tps_min_ = 999.0; tps_min_tick_ = 0;
        tps_drops_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(profiler_mtx_);
        slow_ticks_.clear();
        total_ticks_monitored_ = 0;
    }
    {
        std::lock_guard<std::mutex> lk(packet_mtx_);
        packet_log_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(watchdog_mtx_);
        watchdog_log_.clear();
        watchdog_stalls_200_ = 0;
        watchdog_stalls_500_ = 0;
        watchdog_worst_stall_ms_ = 0;
        watchdog_total_lag_ms_ = 0;
        watchdog_samples_ = 0;
    }
    {
        std::lock_guard<std::mutex> lk(disconnect_mtx_);
        disconnects_.clear();
    }
    raklib_pkts_sent = 0;
    raklib_pkts_recv = 0;
    raklib_retransmits = 0;
    raklib_acks_sent = 0;
    raklib_nacks_sent = 0;
    raklib_pings_recv = 0;
    raklib_game_pkts_recv = 0;
    last_drain_time_ms = now_ms();

    // Start watchdog thread
    watchdog_stop_.store(false, std::memory_order_relaxed);
    watchdog_thread_ = std::thread(&WorkbenchManager::watchdogLoop, this);

    utils::MainLogger::get().info("[WORKBENCH] Diagnostics started. Use /workbench stop to export.");
}

void WorkbenchManager::stop() {
    if (!active_.load(std::memory_order_relaxed)) {
        utils::MainLogger::get().info("[WORKBENCH] Not active.");
        return;
    }
    active_.store(false, std::memory_order_relaxed);

    // Stop watchdog
    watchdog_stop_.store(true, std::memory_order_relaxed);
    if (watchdog_thread_.joinable()) watchdog_thread_.join();

    // Export all reports
    std::string dir = "./workbench/" + timestampStr() + "/";
    ::mkdir("./workbench", 0755);
    ::mkdir(dir.c_str(), 0755);
    exportAll(dir);

    utils::MainLogger::get().info("[WORKBENCH] Session exported to " + dir);
}

void WorkbenchManager::report() {
    if (!active_.load(std::memory_order_relaxed)) {
        utils::MainLogger::get().info("[WORKBENCH] Not active. Use /workbench start first.");
        return;
    }
    std::string dir = "./workbench/" + timestampStr() + "_snapshot/";
    ::mkdir("./workbench", 0755);
    ::mkdir(dir.c_str(), 0755);
    exportAll(dir);
    utils::MainLogger::get().info("[WORKBENCH] Snapshot exported to " + dir);
}

void WorkbenchManager::status() {
    if (!active_.load(std::memory_order_relaxed)) {
        utils::MainLogger::get().info("[WORKBENCH] Status: INACTIVE");
        return;
    }
    uint64_t elapsed = now_ms() - start_time_ms_;

    utils::MainLogger::get().info("[WORKBENCH] Status: ACTIVE (" +
        formatDuration(elapsed) + ")");
    utils::MainLogger::get().info("  TPS samples: " + std::to_string(tps_count_) +
        " | min TPS: " + std::to_string(tps_min_));
    utils::MainLogger::get().info("  Slow ticks (>10ms): " + std::to_string(slow_ticks_.size()));
    utils::MainLogger::get().info("  Packet log entries: " + std::to_string(packet_log_.size()));
    utils::MainLogger::get().info("  RakLib: sent=" + std::to_string(raklib_pkts_sent.load()) +
        " recv=" + std::to_string(raklib_pkts_recv.load()) +
        " retransmits=" + std::to_string(raklib_retransmits.load()));
    utils::MainLogger::get().info("  Watchdog stalls: >200ms=" +
        std::to_string(watchdog_stalls_200_) +
        " >500ms=" + std::to_string(watchdog_stalls_500_));
    utils::MainLogger::get().info("  Disconnects captured: " + std::to_string(disconnects_.size()));
}

WorkbenchManager::~WorkbenchManager() {
    if (active_.load(std::memory_order_relaxed)) {
        active_.store(false, std::memory_order_relaxed);
        watchdog_stop_.store(true, std::memory_order_relaxed);
        if (watchdog_thread_.joinable()) watchdog_thread_.join();
    }
}

// ============================================================
// Hook points
// ============================================================

void WorkbenchManager::onTickStart() {
    if (!active_.load(std::memory_order_relaxed)) return;
    current_sample_ = {};
    current_sample_.tick_id = ++current_tick_id_;
    total_ticks_monitored_++;
}

void WorkbenchManager::onTickPhase(const char* phase_name, uint64_t duration_ms) {
    if (!active_.load(std::memory_order_relaxed)) return;
    std::string p(phase_name);
    if (p == "scheduler") current_sample_.scheduler_ms = duration_ms;
    else if (p == "level") current_sample_.level_ms = duration_ms;
    else if (p == "network") current_sample_.network_ms = duration_ms;
}

void WorkbenchManager::onPlayerUpdate(const std::string& name, uint64_t duration_ms) {
    if (!active_.load(std::memory_order_relaxed)) return;
    current_sample_.player_ms.push_back({name, duration_ms});
}

void WorkbenchManager::onAutosave(uint64_t duration_ms) {
    if (!active_.load(std::memory_order_relaxed)) return;
    current_sample_.autosave_ms = duration_ms;
}

void WorkbenchManager::onTickEnd(uint64_t total_ms) {
    if (!active_.load(std::memory_order_relaxed)) return;
    current_sample_.total_ms = total_ms;

    // TPS monitor
    {
        std::lock_guard<std::mutex> lk(tps_mtx_);
        uint64_t ts = now_ms();
        tick_timestamps_.push_back(ts);
        if (tick_timestamps_.size() > 20) tick_timestamps_.pop_front();

        // Calculate TPS: count ticks in the last 1000ms
        uint64_t one_sec_ago = ts - 1000;
        int ticks_last_sec = 0;
        for (auto& t : tick_timestamps_) {
            if (t >= one_sec_ago) ticks_last_sec++;
        }
        double tps = (double)ticks_last_sec;
        tps_sum_ += tps;
        tps_count_++;
        if (tps < tps_min_) {
            tps_min_ = tps;
            tps_min_tick_ = current_sample_.tick_id;
        }

        // Log every 20 ticks (~1s)
        if (current_sample_.tick_id % 20 == 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[%s] TPS=%.1f tick=%llu",
                           time_str().c_str(), tps,
                           (unsigned long long)current_sample_.tick_id);
            std::string line = buf;
            if (tps < 15.0) {
                line += "  \xe2\x86\x90 WARN: TPS drop";
                tps_drops_.push_back({current_sample_.tick_id, tps});
                if (tps < 10.0) line += " CRITICAL";
            }
            tps_log_.push_back(line);
        }
    }

    // Tick profiler: store slow ticks
    if (total_ms > 10) {
        std::lock_guard<std::mutex> lk(profiler_mtx_);
        slow_ticks_.push_back(current_sample_);
        if (slow_ticks_.size() > 1000) slow_ticks_.pop_front();
    }
}

void WorkbenchManager::onPacketIn(const std::string& ip_port, uint8_t pid, size_t size) {
    if (!active_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(packet_mtx_);
    packet_log_.push_back({time_str(), "IN ", ip_port, pid, size});
    if (packet_log_.size() > MAX_PACKET_LOG) packet_log_.pop_front();
}

void WorkbenchManager::onPacketOut(const std::string& player, uint8_t pid, size_t size) {
    if (!active_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(packet_mtx_);
    packet_log_.push_back({time_str(), "OUT", player, pid, size});
    if (packet_log_.size() > MAX_PACKET_LOG) packet_log_.pop_front();
}

void WorkbenchManager::onDisconnect(const std::string& player, const std::string& reason,
                                       uint64_t session_ms, uint64_t last_activity_delta_ms,
                                       size_t chunk_queue_pending) {
    if (!active_.load(std::memory_order_relaxed)) return;

    DisconnectDump dump;
    dump.player_name = player;
    dump.reason = reason;
    dump.session_duration_ms = session_ms;
    dump.last_activity_delta_ms = last_activity_delta_ms;
    dump.chunk_queue_pending = chunk_queue_pending;

    // Capture last 20 packets for this player
    {
        std::lock_guard<std::mutex> lk(packet_mtx_);
        int count = 0;
        std::vector<PacketLogEntry> temp;
        for (auto it = packet_log_.rbegin(); it != packet_log_.rend() && count < 20; ++it) {
            if (it->player.find(player) != std::string::npos ||
                it->direction == "IN ") {
                temp.push_back(*it);
                count++;
            }
        }
        // Reverse to chronological order
        dump.last_packets.assign(temp.rbegin(), temp.rend());
    }

    // TPS last 5s
    {
        std::lock_guard<std::mutex> lk(tps_mtx_);
        // Simple: just report current TPS 5 times
        for (int i = 0; i < 5; ++i) {
            dump.tps_last_5s.push_back(tps_min_ > 0 ? tps_sum_ / std::max((uint64_t)1, tps_count_) : 20.0);
        }
    }

    // Tick durations last 5
    {
        std::lock_guard<std::mutex> lk(profiler_mtx_);
        int count = 0;
        for (auto it = slow_ticks_.rbegin(); it != slow_ticks_.rend() && count < 5; ++it) {
            dump.tick_durations_last_5.push_back(it->total_ms);
            count++;
        }
    }

    // Auto-diagnosis
    if (reason == "timeout" && last_activity_delta_ms > 10000) {
        dump.diagnosis = "Player sent no game packets for " +
            std::to_string(last_activity_delta_ms / 1000) +
            "s. SESSION_TIMEOUT fired. Consider:\n"
            "  - Check if autosave blocked the main thread (see tick_profiler.txt)\n"
            "  - Increase session timeout in SessionManager.c (currently 15000ms)\n"
            "  - Make autosave asynchronous";
    } else if (reason.find("kick") != std::string::npos) {
        dump.diagnosis = "Player was kicked by admin/command.";
    } else {
        dump.diagnosis = "Normal disconnect.";
    }

    {
        std::lock_guard<std::mutex> lk(disconnect_mtx_);
        disconnects_.push_back(std::move(dump));
    }

    utils::MainLogger::get().info("[WORKBENCH] Disconnect captured: " + player +
        " (" + reason + ") — " + std::to_string(last_activity_delta_ms / 1000) + "s since last activity");
}

// ============================================================
// Watchdog thread
// ============================================================

void WorkbenchManager::watchdogLoop() {
    while (!watchdog_stop_.load(std::memory_order_relaxed)) {
        uint64_t now = now_ms();
        uint64_t last = last_drain_time_ms.load(std::memory_order_relaxed);
        uint64_t lag = (now > last) ? (now - last) : 0;

        {
            std::lock_guard<std::mutex> lk(watchdog_mtx_);
            char buf[128];
            const char* level = "OK";
            if (lag > 200) {
                level = "WARN";
                watchdog_stalls_200_++;
                if (lag > 500) {
                    level = "CRITICAL";
                    watchdog_stalls_500_++;
                }
                if (lag > watchdog_worst_stall_ms_) {
                    watchdog_worst_stall_ms_ = lag;
                    watchdog_worst_stall_time_ = time_str();
                }
            }
            std::snprintf(buf, sizeof(buf), "[%s] [WATCHDOG] %s — drain lag: %llums",
                           time_str().c_str(), level, (unsigned long long)lag);
            watchdog_log_.push_back(buf);
            if (watchdog_log_.size() > 10000) watchdog_log_.pop_front();
            watchdog_total_lag_ms_ += lag;
            watchdog_samples_++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ============================================================
// Export functions
// ============================================================

void WorkbenchManager::exportAll(const std::string& dir) {
    exportTps(dir);
    exportTickProfiler(dir);
    exportPackets(dir);

    // RakLib stats
    {
        std::ofstream f(dir + "raklib.txt");
        f << "RAKLIB STATS REPORT\n";
        f << "===================\n";
        f << "Session duration: " << formatDuration(now_ms() - start_time_ms_) << "\n\n";
        f << "Global:\n";
        f << "  Packets sent:       " << raklib_pkts_sent.load() << "\n";
        f << "  Packets received:   " << raklib_pkts_recv.load() << "\n";
        f << "  Retransmits:        " << raklib_retransmits.load() << "\n";
        f << "  ACKs sent:          " << raklib_acks_sent.load() << "\n";
        f << "  NACKs sent:         " << raklib_nacks_sent.load() << "\n";
        f << "  PINGs received:     " << raklib_pings_recv.load() << "\n";
        f << "  Game pkts received: " << raklib_game_pkts_recv.load() << "\n";
    }

    // Watchdog
    {
        std::lock_guard<std::mutex> lk(watchdog_mtx_);
        std::ofstream f(dir + "watchdog.txt");
        f << "WATCHDOG REPORT\n";
        f << "===============\n";
        f << "Total stalls (>200ms): " << watchdog_stalls_200_ << "\n";
        f << "Total stalls (>500ms): " << watchdog_stalls_500_ << "\n";
        f << "Worst stall: " << watchdog_worst_stall_ms_ << "ms at "
          << watchdog_worst_stall_time_ << "\n";
        f << "Average drain lag: "
          << (watchdog_samples_ > 0 ? watchdog_total_lag_ms_ / watchdog_samples_ : 0)
          << "ms\n\n";
        f << "--- Log (last 1000 entries) ---\n";
        size_t start = watchdog_log_.size() > 1000 ? watchdog_log_.size() - 1000 : 0;
        for (size_t i = start; i < watchdog_log_.size(); ++i) {
            f << watchdog_log_[i] << "\n";
        }
    }

    exportDisconnects(dir);
    exportSummary(dir);

    // Print summary to console
    double tps_avg = tps_count_ > 0 ? tps_sum_ / tps_count_ : 0;
    utils::MainLogger::get().info("[WORKBENCH] Summary: TPS avg=" +
        std::to_string(tps_avg).substr(0, 4) +
        " min=" + std::to_string(tps_min_).substr(0, 4) +
        " | Disconnects=" + std::to_string(disconnects_.size()) +
        " | Stalls=" + std::to_string(watchdog_stalls_200_));

    // Auto-diagnosis
    std::string diagnosis;
    {
        std::lock_guard<std::mutex> lk(disconnect_mtx_);
        for (auto& d : disconnects_) {
            if (d.reason == "timeout") {
                diagnosis = "SESSION_TIMEOUT too low (15s) — see disconnects.txt";
                break;
            }
        }
    }
    if (!slow_ticks_.empty()) {
        auto worst = std::max_element(slow_ticks_.begin(), slow_ticks_.end(),
            [](const TickSample& a, const TickSample& b) {
                return a.total_ms < b.total_ms;
            });
        if (worst->total_ms > 100 && worst->autosave_ms > 50) {
            diagnosis = "Autosave blocking main thread (" +
                std::to_string(worst->autosave_ms) + "ms) — see tick_profiler.txt";
        }
    }
    if (!diagnosis.empty()) {
        utils::MainLogger::get().info("[WORKBENCH] Diagnosis: " + diagnosis);
    }
}

void WorkbenchManager::exportTps(const std::string& dir) {
    std::lock_guard<std::mutex> lk(tps_mtx_);
    std::ofstream f(dir + "tps.txt");
    f << "TPS MONITOR REPORT\n";
    f << "==================\n";
    f << "Total ticks monitored: " << total_ticks_monitored_ << "\n";
    f << "TPS samples: " << tps_count_ << "\n";
    double avg = tps_count_ > 0 ? tps_sum_ / tps_count_ : 0;
    f << "TPS average: " << std::fixed << std::setprecision(1) << avg << "\n";
    f << "TPS minimum: " << std::setprecision(1) << tps_min_
      << " (tick #" << tps_min_tick_ << ")\n\n";
    f << "TPS drops (<15):\n";
    for (auto& [tick, tps] : tps_drops_) {
        f << "  tick #" << tick << " TPS=" << std::setprecision(1) << tps << "\n";
    }
    f << "\n--- Continuous log ---\n";
    for (auto& line : tps_log_) f << line << "\n";
}

void WorkbenchManager::exportTickProfiler(const std::string& dir) {
    std::lock_guard<std::mutex> lk(profiler_mtx_);
    std::ofstream f(dir + "tick_profiler.txt");
    f << "TICK PROFILER REPORT\n";
    f << "====================\n";
    f << "Total ticks monitored: " << total_ticks_monitored_ << "\n";
    f << "Slow ticks (>10ms): " << slow_ticks_.size() << "\n\n";

    if (slow_ticks_.empty()) {
        f << "No slow ticks detected.\n";
        return;
    }

    // Find worst
    auto worst = std::max_element(slow_ticks_.begin(), slow_ticks_.end(),
        [](const TickSample& a, const TickSample& b) {
            return a.total_ms < b.total_ms;
        });
    f << "Worst tick: #" << worst->tick_id << " — " << worst->total_ms << "ms\n";
    f << "  scheduler:  " << worst->scheduler_ms << "ms\n";
    f << "  level:      " << worst->level_ms << "ms\n";
    f << "  network:    " << worst->network_ms << "ms\n";
    f << "  autosave:   " << worst->autosave_ms << "ms\n";
    for (auto& [name, ms] : worst->player_ms) {
        f << "  player[" << name << "]: " << ms << "ms\n";
    }
    f << "\n";

    // Top 10 worst
    std::vector<TickSample> sorted(slow_ticks_.begin(), slow_ticks_.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const TickSample& a, const TickSample& b) {
            return a.total_ms > b.total_ms;
        });
    f << "TOP 10 WORST TICKS:\n";
    for (size_t i = 0; i < std::min(sorted.size(), (size_t)10); ++i) {
        f << "  #" << sorted[i].tick_id << "  " << sorted[i].total_ms << "ms";
        if (sorted[i].autosave_ms > 0) f << "  — AUTOSAVE";
        if (!sorted[i].player_ms.empty()) {
            auto& [pname, pms] = sorted[i].player_ms[0];
            if (pms > 5) f << "  — player[" << pname << "]:" << pms << "ms";
        }
        f << "\n";
    }
}

void WorkbenchManager::exportPackets(const std::string& dir) {
    std::lock_guard<std::mutex> lk(packet_mtx_);
    std::ofstream f(dir + "packets.txt");
    f << "PACKET LOG (" << packet_log_.size() << " entries)\n";
    f << "=========================================\n\n";
    for (auto& e : packet_log_) {
        f << "[" << e.timestamp << "] " << e.direction << "  "
          << e.player << "  " << pidToName(e.pid)
          << "(0x" << std::hex << (int)e.pid << std::dec << ")  "
          << e.size << " bytes\n";
    }
}

void WorkbenchManager::exportDisconnects(const std::string& dir) {
    std::lock_guard<std::mutex> lk(disconnect_mtx_);
    std::ofstream f(dir + "disconnects.txt");
    f << "DISCONNECT REPORT (" << disconnects_.size() << " events)\n";
    f << "==========================================\n\n";
    for (auto& d : disconnects_) {
        f << "========================================\n";
        f << "DISCONNECT: " << d.player_name << "\n";
        f << "Reason: " << d.reason << "\n";
        f << "Session duration: " << formatDuration(d.session_duration_ms) << "\n";
        f << "Last activity delta: " << d.last_activity_delta_ms << "ms\n";
        f << "Chunk queue pending: " << d.chunk_queue_pending << "\n";
        f << "========================================\n";
        f << "Last packets received:\n";
        for (auto& p : d.last_packets) {
            f << "  " << p.timestamp << "  " << pidToName(p.pid)
              << "  " << p.size << " bytes\n";
        }
        f << "\nTPS last 5s: ";
        for (size_t i = 0; i < d.tps_last_5s.size(); ++i) {
            f << "t-" << (d.tps_last_5s.size() - i) << "s: "
              << std::fixed << std::setprecision(1) << d.tps_last_5s[i] << " ";
        }
        f << "\nTick durations last 5: ";
        for (size_t i = 0; i < d.tick_durations_last_5.size(); ++i) {
            f << d.tick_durations_last_5[i] << "ms ";
        }
        f << "\n\n*** DIAGNOSIS:\n" << d.diagnosis << "\n\n";
    }
}

void WorkbenchManager::exportSummary(const std::string& dir) {
    std::ofstream f(dir + "summary.txt");
    f << "WORKBENCH SUMMARY\n";
    f << "=================\n";
    f << "Duration: " << formatDuration(now_ms() - start_time_ms_) << "\n";
    f << "Ticks monitored: " << total_ticks_monitored_ << "\n";
    double tps_avg = tps_count_ > 0 ? tps_sum_ / tps_count_ : 0;
    f << "TPS average: " << std::fixed << std::setprecision(1) << tps_avg << "\n";
    f << "TPS minimum: " << std::setprecision(1) << tps_min_ << "\n";
    f << "Slow ticks (>10ms): " << slow_ticks_.size() << "\n";
    if (!slow_ticks_.empty()) {
        auto worst = std::max_element(slow_ticks_.begin(), slow_ticks_.end(),
            [](const TickSample& a, const TickSample& b) {
                return a.total_ms < b.total_ms;
            });
        f << "Worst tick: #" << worst->tick_id << " — " << worst->total_ms << "ms";
        if (worst->autosave_ms > 0) f << " (autosave: " << worst->autosave_ms << "ms)";
        f << "\n";
    }
    f << "Disconnects: " << disconnects_.size() << "\n";
    f << "Watchdog stalls (>200ms): " << watchdog_stalls_200_ << "\n";
    f << "Watchdog stalls (>500ms): " << watchdog_stalls_500_ << "\n";
    f << "\n--- AUTO-DIAGNOSIS ---\n";
    // Check for known patterns
    bool found = false;
    for (auto& d : disconnects_) {
        if (d.reason == "timeout") {
            f << "* SESSION_TIMEOUT too low (15s) — see disconnects.txt\n";
            f << "  Player " << d.player_name << " timed out after "
              << d.last_activity_delta_ms << "ms without game packets.\n";
            found = true;
            break;
        }
    }
    if (!slow_ticks_.empty()) {
        auto worst = std::max_element(slow_ticks_.begin(), slow_ticks_.end(),
            [](const TickSample& a, const TickSample& b) {
                return a.total_ms < b.total_ms;
            });
        if (worst->autosave_ms > 50) {
            f << "* Autosave blocking main thread (" << worst->autosave_ms
              << "ms on tick #" << worst->tick_id << ")\n";
            f << "  Consider making saveAll() asynchronous.\n";
            found = true;
        }
        if (worst->total_ms > 100 && !worst->player_ms.empty()) {
            auto& [name, ms] = worst->player_ms[0];
            if (ms > 50) {
                f << "* Player update blocking on tick #" << worst->tick_id
                  << " (" << ms << "ms for " << name << ")\n";
                f << "  Likely chunk streaming. Consider spreading across ticks.\n";
                found = true;
            }
        }
    }
    if (!found) {
        f << "* No known issues detected.\n";
    }
}

} // namespace cursestone
