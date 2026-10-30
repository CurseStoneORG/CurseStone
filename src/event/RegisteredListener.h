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
// RegisteredListener.h — a single listener subscription to
// un HandlerList (es decir, a UN tipo concreto de evento). Guarda el
// callback type-erased junto con los metadatos necesarios para el
// despacho ordenado por prioridad y para poder darse de baja en bloque.
//
// `owner` identifies who registered this callback (usually the
// puntero `this` de un objeto que implementa Listener, pero puede ser
// any stable address the caller controls. The event system NEVER
// dereferences it: it is only used as a comparison key
// para unregisterOwner()/unlistenAll(), de modo que cuando un objeto se
// destruye pueda limpiar todas sus suscripciones sin dejar callbacks
// colgantes que apunten a memoria liberada.
#pragma once
#include "Event.h"
#include "EventPriority.h"
#include <functional>
#include <cstdint>

namespace cursestone::event {

struct RegisteredListener {
    uint64_t id = 0;
    void* owner = nullptr;
    EventPriority priority = EventPriority::NORMAL;
    bool ignoreCancelled = false;
    std::function<void(Event&)> callback;
};

} // namespace cursestone::event
