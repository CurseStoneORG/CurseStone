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
#include "HandlerList.h"
#include "Cancellable.h"
#include "utils/MainLogger.hpp"
#include <algorithm>
#include <exception>
#include <string>

namespace cursestone::event {

uint64_t HandlerList::registerListener(void* owner, EventPriority priority, bool ignoreCancelled,
                                       std::function<void(Event&)> callback) {
    if (!callback)
        return 0;
    std::lock_guard<std::mutex> lk(mtx_);
    uint64_t id = next_id_++;
    RegisteredListener rl;
    rl.id = id;
    rl.owner = owner;
    rl.priority = priority;
    rl.ignoreCancelled = ignoreCancelled;
    rl.callback = std::move(callback);
    slots_[static_cast<size_t>(priority)].push_back(std::move(rl));
    return id;
}

bool HandlerList::unregister(uint64_t id) {
    if (id == 0)
        return false;
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& slot : slots_) {
        auto it = std::find_if(slot.begin(), slot.end(), [id](const RegisteredListener& rl) {
            return rl.id == id;
        });
        if (it != slot.end()) {
            slot.erase(it);
            return true;
        }
    }
    return false;
}

size_t HandlerList::unregisterOwner(void* owner) {
    if (!owner)
        return 0;
    std::lock_guard<std::mutex> lk(mtx_);
    size_t removed = 0;
    for (auto& slot : slots_) {
        size_t before = slot.size();
        slot.erase(std::remove_if(slot.begin(), slot.end(),
                                  [owner](const RegisteredListener& rl) {
                                      return rl.owner == owner;
                                  }),
                   slot.end());
        removed += before - slot.size();
    }
    return removed;
}

void HandlerList::unregisterAll() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& slot : slots_)
        slot.clear();
}

size_t HandlerList::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    size_t n = 0;
    for (auto& slot : slots_)
        n += slot.size();
    return n;
}

void HandlerList::dispatch(Event& event) const {
    // Snapshot de cada slot bajo el lock, en orden de prioridad, y
    // luego se ejecutan los callbacks fuera de él. Esto permite que un
    // handler registre/desregistre listeners (incluso para este mismo
    // tipo de evento) desde dentro de su propio callback sin deadlock
    // contra mtx_.
    std::vector<RegisteredListener> due;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        size_t total = 0;
        for (auto& slot : slots_)
            total += slot.size();
        due.reserve(total);
        for (auto& slot : slots_) {
            for (auto& rl : slot)
                due.push_back(rl); // copia el std::function
        }
    }

    // dynamic_cast funciona aquí porque tanto Event como Cancellable
    // tienen destructor virtual (son polimórficos).
    Cancellable* cancellable = dynamic_cast<Cancellable*>(&event);

    for (auto& rl : due) {
        if (cancellable && cancellable->isCancelled() && rl.ignoreCancelled) {
            continue;
        }
        try {
            if (rl.callback)
                rl.callback(event);
        } catch (const std::exception& e) {
            utils::MainLogger::get().warn(
                std::string("[Events] Un listener lanzo una excepcion durante ") +
                event.eventName() + ": " + e.what() +
                " -- listener omitido, el resto siguio ejecutandose.");
        } catch (...) {
            utils::MainLogger::get().warn(
                std::string("[Events] Un listener lanzo una excepcion no-std durante ") +
                event.eventName() + " -- listener omitido, el resto siguio ejecutandose.");
        }
    }
}

} // namespace cursestone::event
