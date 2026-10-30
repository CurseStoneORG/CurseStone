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
// HandlerList — el conjunto de listeners registrados para UN tipo
// concreto de evento, organizados en buckets por EventPriority. Cada
// tipo de evento tiene su propio HandlerList, administrado por
// EventManager (ver EventManager.h).
//
// STABILITY CONTRACT (this replaces an earlier version that never got
// wired to the rest of the code and had two latent bugs: dispatch()
// took the event as `const Event&`, so no listener could ever call
// setCancelled(); and EventHandler.h's priority/ignoreCancelled fields
// were never consulted anywhere):
//
//   - dispatch() takes a non-const Event& so listeners can both read
//     and modify the event (cancel it, rewrite a chat message, etc).
//   - Listeners run in EventPriority order (LOWEST first, MONITOR
//     last), so higher priorities see what lower ones produced and
//     MONITOR listeners always see the final state.
//   - A listener registered with ignoreCancelled=true is skipped as
//     soon as the event was already cancelled by an earlier listener —
//     replica RegisteredListener::callEvent() del servidor de referencia.
//   - A listener that throws is caught, logged and skipped: a broken
//     handler can never take down the tick loop nor block the rest
//     (same stability doctrine as the post-fire-cascade scheduler).
//   - All mutations (register/unregister) are mutex-protected, so it
//     can be used from any thread. dispatch() snapshots under the lock
//     and runs callbacks outside of it, so a handler can (un)register
//     listeners without deadlocking against itself.
#pragma once
#include "Event.h"
#include "EventPriority.h"
#include "RegisteredListener.h"
#include <array>
#include <vector>
#include <mutex>
#include <cstdint>
#include <functional>

namespace cursestone::event {

class HandlerList {
  public:
    // Registra `callback` en la prioridad dada. Devuelve un id de
    // registro utilizable con unregister(). Devuelve 0 si callback
    // is empty (nothing to register).
    uint64_t registerListener(void* owner, EventPriority priority, bool ignoreCancelled,
                              std::function<void(Event&)> callback);

    // Removes a single registration by id. true if found.
    bool unregister(uint64_t id);

    // Elimina todos los registros cuyo `owner` coincida. Devuelve
    // how many were removed. Used when a Listener is destroyed.
    size_t unregisterOwner(void* owner);

    // Elimina absolutamente todos los registros.
    void unregisterAll();

    // Ejecuta cada listener registrado para `event`, en orden de
    // priority, skipping ignoreCancelled handlers once the event is
    // cancelado. Segura de llamar mientras otros hilos registran o
    // desregistran listeners concurrentemente.
    void dispatch(Event& event) const;

    size_t size() const;

  private:
    mutable std::mutex mtx_;
    std::array<std::vector<RegisteredListener>, kEventPriorityCount> slots_;
    uint64_t next_id_ = 1;
};

} // namespace cursestone::event
