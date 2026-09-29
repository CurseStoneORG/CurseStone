// RegisteredListener.h — una suscripción individual de un listener a
// un HandlerList (es decir, a UN tipo concreto de evento). Guarda el
// callback type-erased junto con los metadatos necesarios para el
// despacho ordenado por prioridad y para poder darse de baja en bloque.
//
// `owner` identifica quién registró este callback (normalmente el
// puntero `this` de un objeto que implementa Listener, pero puede ser
// cualquier dirección estable que el llamador controle). El sistema de
// eventos NUNCA lo desreferencia: solo lo usa como clave de comparación
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
