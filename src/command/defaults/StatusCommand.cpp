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
// StatusCommand.cpp — /status implementation.
#include "StatusCommand.h"
#include "Server.h"
#include "Player.h"
#include "MemoryManager.h"
#include "command/ConsoleCommandSender.h"
#include "diagnostics/Diagnostics.h"
#include "plugin/PluginManager.h"
#include "scheduler/CallbackTask.h"
#include "scheduler/ServerScheduler.h"
#include "utils/TextColor.h"
#include "world/World.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cursestone {

namespace {

std::string formatUptime(uint64_t sec) {
    uint64_t d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60, s = sec % 60;
    std::string out;
    if (d)
        out += std::to_string(d) + "d ";
    if (d || h)
        out += std::to_string(h) + "h ";
    out += std::to_string(m) + "m " + std::to_string(s) + "s";
    return out;
}

std::string fixed2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

const std::string& tpsColor(int tps) {
    if (tps >= 18)
        return TC::GREEN;
    if (tps >= 15)
        return TC::YELLOW;
    return TC::RED;
}

const std::string& msColor(double ms) {
    if (ms < 25.0)
        return TC::GREEN;
    if (ms < 45.0)
        return TC::YELLOW;
    return TC::RED;
}

// Reading /proc blocks on I/O, so the RSS value is reused for a short time. Main thread only.
int64_t cachedRssKB() {
    using Clock = std::chrono::steady_clock;
    static int64_t value = 0;
    static Clock::time_point stamp{};
    auto now = Clock::now();
    if (stamp == Clock::time_point{} || now - stamp > std::chrono::seconds(2)) {
        value = MemoryManager::get().currentUsageKB();
        stamp = now;
    }
    return value;
}

std::vector<std::string> buildStatusLines() {
    Server& srv = Server::getInstance();
    const std::string& G = TC::GRAY;
    const std::string& W = TC::WHITE;
    std::vector<std::string> lines;

    lines.push_back(TC::GOLD + "=== Server Status ===" + TC::RESET);

    int tps = srv.getTPS();
    lines.push_back(G + "TPS: " + tpsColor(tps) + std::to_string(tps) + W + "/20" + TC::RESET);

    int64_t rssKB = cachedRssKB();
    int limitMB = MemoryManager::get().limitMB();
    std::string ram = rssKB > 0 ? std::to_string(rssKB / 1024) + " MB" : "n/a";
    if (limitMB > 0)
        ram += " / " + std::to_string(limitMB) + " MB limit";
    lines.push_back(G + "RAM: " + W + ram + TC::RESET);

    lines.push_back(G + "Uptime: " + W +
                    formatUptime(diagnostics::Diagnostics::get().uptimeSeconds()) + TC::RESET);

    auto players = srv.playersSnapshot();
    lines.push_back(G + "Players: " + W + std::to_string(players.size()) + "/" +
                    std::to_string(srv.maxPlayers()) + TC::RESET);

    auto plugins = plugin::PluginManager::get().list();
    size_t enabled = 0;
    std::string names;
    for (const auto& p : plugins) {
        if (p.enabled)
            ++enabled;
        if (!names.empty())
            names += ", ";
        names += p.name + (p.enabled ? "" : " (disabled)");
    }
    lines.push_back(G + "Plugins (" + W + std::to_string(enabled) + "/" +
                    std::to_string(plugins.size()) + G + "): " + W +
                    (names.empty() ? "none" : names) + TC::RESET);

    lines.push_back(G + "Worlds:" + TC::RESET);
    for (World* w : srv.getWorlds()) {
        if (!w)
            continue;
        size_t inWorld = 0;
        for (const auto& p : players) {
            if (p && p->getWorld() == w)
                ++inWorld;
        }
        lines.push_back(G + "  " + W + w->name() + G + ": " + msColor(w->avgTickMs()) +
                        fixed2(w->avgTickMs()) + " ms" + G + " avg, " + W +
                        fixed2(w->lastTickMs()) + " ms" + G + " last | chunks " + W +
                        std::to_string(w->chunks().count()) + G + " | players " + W +
                        std::to_string(inWorld) + TC::RESET);
    }
    return lines;
}

// Players get one packet instead of one per line.
void sendStatus(CommandSender& out, bool singleMessage) {
    std::vector<std::string> lines = buildStatusLines();
    if (!singleMessage) {
        for (const auto& line : lines)
            out.sendMessage(line);
        return;
    }
    std::string joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i)
            joined += '\n';
        joined += lines[i];
    }
    out.sendMessage(joined);
}

constexpr uint64_t kPlayerCooldownTicks = 40;

} // namespace

bool StatusCommand::execute(CommandSender* sender, const std::string&,
                            const std::vector<std::string>&) {
    if (!sender->isOp()) {
        sender->sendMessage(TC::RED + "You don't have permission to use this command." + TC::RESET);
        return true;
    }

    // Player commands already run on the main thread, so no scheduling hop is needed.
    if (auto* player = dynamic_cast<Player*>(sender)) {
        static std::unordered_map<std::string, uint64_t> lastUse;
        uint64_t tick = Server::getInstance().getCurrentTick();
        uint64_t& last = lastUse[player->getName()];
        if (last != 0 && tick - last < kPlayerCooldownTicks) {
            sender->sendMessage(TC::RED + "Wait a moment before using /status again." + TC::RESET);
            return true;
        }
        last = tick;
        sendStatus(*player, true);
        return true;
    }

    // The sender pointer does not outlive this call for console commands, so resolve it by
    // name inside the main-thread task.
    std::string senderName = sender->getName();
    auto task = std::make_shared<scheduler::CallbackTask>([senderName]() {
        if (senderName == "CONSOLE") {
            ConsoleCommandSender console;
            sendStatus(console, false);
        } else if (Player* p = Server::getInstance().getPlayerExact(senderName)) {
            sendStatus(*p, true);
        }
    });
    scheduler::ServerScheduler::get().scheduleSync(task, 0, 0);
    return true;
}

} // namespace cursestone
