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
#include "EventManager.h"
#include <vector>

namespace cursestone::event {

EventManager& EventManager::get() {
    static EventManager instance;
    return instance;
}

HandlerList& EventManager::listFor(const std::type_index& idx) {
    std::lock_guard<std::mutex> lk(map_mtx_);
    auto it = lists_.find(idx);
    if (it == lists_.end()) {
        it = lists_.emplace(idx, std::make_unique<HandlerList>()).first;
    }
    return *it->second;
}

HandlerList* EventManager::findList(const std::type_index& idx) const {
    std::lock_guard<std::mutex> lk(map_mtx_);
    auto it = lists_.find(idx);
    return it == lists_.end() ? nullptr : it->second.get();
}

size_t EventManager::unlistenAll(void* owner) {
    if (!owner)
        return 0;
    // Snapshot de los HandlerList* bajo map_mtx_, y luego se llama a
    // unregisterOwner() en cada uno FUERA de ese lock — cada
    // HandlerList tiene su propio mutex, así que no hace falta
    // mantener map_mtx_ tomado mientras tanto, y evita anidar los dos
    // dominios de lock.
    std::vector<HandlerList*> all;
    {
        std::lock_guard<std::mutex> lk(map_mtx_);
        all.reserve(lists_.size());
        for (auto& kv : lists_)
            all.push_back(kv.second.get());
    }
    size_t total = 0;
    for (auto* list : all)
        total += list->unregisterOwner(owner);
    return total;
}

void EventManager::call(Event& event) {
    // typeid(event) resuelve el tipo DINÁMICO (la subclase concreta)
    // porque Event es polimórfico (tiene destructor virtual).
    HandlerList* list = findList(std::type_index(typeid(event)));
    if (!list)
        return; // nadie escucha este tipo concreto todavia.
    list->dispatch(event);
}

} // namespace cursestone::event
