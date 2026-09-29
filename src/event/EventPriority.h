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
