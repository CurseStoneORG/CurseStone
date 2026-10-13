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
// EventPriority.h — orden en que se llama a los listeners de un mismo
// evento. Las prioridades más bajas se ejecutan primero, dejando a las
// más altas la última palabra sobre el resultado del evento.
//
// Antes de este cambio este archivo NO EXISTÍA aunque EventHandler.h
// ya lo incluía (`#include "EventPriority.h"`) — es decir, el sistema
// de eventos no compilaba en cuanto algo incluyera EventHandler.h.
//
// Orden de llamada: LOWEST -> LOW -> NORMAL -> HIGH -> HIGHEST -> MONITOR
//
// Los listeners MONITOR se usan solo para observar el resultado final:
// se ejecutan últimos, después de que cualquier otra prioridad haya
// tenido oportunidad de cancelar o modificar el evento, y no deberían
// alterar el estado del evento ellos mismos.
#pragma once
#include <cstdint>

namespace cursestone::event {

enum class EventPriority : uint8_t {
    LOWEST  = 0,
    LOW     = 1,
    NORMAL  = 2,
    HIGH    = 3,
    HIGHEST = 4,
    MONITOR = 5,
};

// Número de slots de prioridad — usado por HandlerList para tamañar
// su array de buckets. Mantener en sync con el enum de arriba.
constexpr int kEventPriorityCount = 6;

} // namespace cursestone::event
