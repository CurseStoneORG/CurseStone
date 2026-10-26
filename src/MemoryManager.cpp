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
#include "MemoryManager.h"

#include "Player.h"
#include "Server.h"
#include "world/World.h"
#include "utils/MainLogger.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace cursestone {

MemoryManager& MemoryManager::get() {
    static MemoryManager m; // the ONE and only global GarbageCollector
    return m;
}

int64_t MemoryManager::currentUsageKB() const {
    // Linux: read /proc/self/status VmRSS. Windows/Android fallbacks keep
    // this a no-op returning 0 (shouldGc() then never fires spuriously).
#if defined(__linux__)
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            size_t i = line.find_first_of("0123456789");
            if (i != std::string::npos)
                return std::stoll(line.substr(i));
        }
    }
#endif
    return 0;
}

bool MemoryManager::shouldGc() const {
    if (limit_mb_ <= 0)
        return false;
    return currentUsageKB() > (int64_t)limit_mb_ * 1024;
}

// The global collection pass. Called from Server::tick() every
// gc_interval_ticks_, on the main thread, once - replacing the old
// scattered independent schedules.
void MemoryManager::collect(bool force) {
    const uint64_t tick = Server::getInstance().getCurrentTick();

    // Rate limit: only one pass per interval. `force` (shutdown / manual)
    // bypasses the gate but never bypasses single-entry exclusivity.
    uint64_t expected = last_collect_tick_.load(std::memory_order_relaxed);
    while (true) {
        if (!force && tick - expected < gc_interval_ticks_)
            return;
        if (last_collect_tick_.compare_exchange_weak(expected, tick, std::memory_order_relaxed,
                                                     std::memory_order_relaxed)) {
            break;
        }
        // expected was reloaded with the current value by the failed CAS;
        // loop back and re-check the interval gate with fresh data.
    }

    // 1) Chunk GC — free chunks no viewer can see, flushed + saved.
    //    Runs here INSTEAD of Server::tick's own 100-tick sweep: same
    //    amount of work, but on the single coordinated GC schedule.
    //    Every world is swept: chunks in non-default worlds were never
    //    evicted before (unbounded growth on Nether/plugin worlds).
    {
        std::vector<world::World::ChunkViewer> viewers;
        {
            auto snap = Server::getInstance().playersSnapshot();
            viewers.reserve(snap.size());
            for (auto& p : snap) {
                if (!p)
                    continue;
                viewers.push_back({p->chunkX(), p->chunkZ(), p->viewDistance()});
            }
        }
        if (!viewers.empty()) {
            for (world::World* lvl : Server::getInstance().getWorlds()) {
                if (!lvl)
                    continue;
                int unloaded = lvl->unloadDistantChunks(viewers);
                if (unloaded > 0 && Server::getInstance().debugMode()) {
                    utils::MainLogger::get().info("[GC] Unloaded " + std::to_string(unloaded) +
                                                  " chunk(s) out of view in '" + lvl->name() +
                                                  "'.");
                }
            }
        }
    }

    // 2) Dropped-item GC — despawn items by AGE (5 min like vanilla),
    //    then drop taken entries. The removal happens on the GC's own
    //    schedule, never mid-pickup inside a player's update handler:
    //    this is what fixes the pickup/rollback desync. Sweeps every
    //    world; items dropped outside the default world were never
    //    erased before (permanent per-entry leak).
    for (world::World* lvl : Server::getInstance().getWorlds()) {
        if (lvl)
            lvl->collectDroppedItems(tick);
    }

#ifdef __GLIBC__
    // 3) Return freed heap to the OS. glibc keeps freed small blocks in
    //    its bins; without this RSS never goes back down.
    malloc_trim(0);
#endif

    collects_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace cursestone
