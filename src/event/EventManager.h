// EventManager — bus de eventos de todo el servidor. Es dueño de UN
// HandlerList por cada subclase concreta de Event (indexado por
// std::type_index vía RTTI sobre el Event base, que es polimórfico) y
// es el único punto de entrada que el resto del código usa para
// disparar eventos y escucharlos.
//
// Esta es la pieza que hace que las ~120 subclases de Event bajo
// src/event/ hagan algo de verdad. Antes de esto:
//   - Ninguna de esas ~120 clases se instanciaba en ningún lugar del
//     código fuera de su propia carpeta.
//   - HandlerList::dispatch() recibía el evento por const-ref, así que
//     ningún listener podía cancelar nada aunque quisiera.
//   - EventPriority.h ni siquiera existía como archivo.
//
// USO — escuchar un evento:
//     EventManager::get().listen<event::block::BlockBreakEvent>(this,
//         [](event::block::BlockBreakEvent& e) {
//             if (algunaCondicion) e.setCancelled();
//         }, EventPriority::HIGH);
//
// USO — disparar un evento ya construido y leer si fue cancelado:
//     event::block::BlockBreakEvent ev(player, pos, blockId);
//     EventManager::get().call(ev);
//     if (ev.isCancelled()) return;
//
// USO — construir + disparar + devolver por valor en un solo paso:
//     auto ev = EventManager::get().fire<event::player::PlayerChatEvent>(
//         this, message);
//     if (ev.isCancelled()) return;
//     broadcast(ev.message); // el listener pudo haber reescrito el mensaje
//
// HILOS: listen()/unlisten()/call() pueden invocarse desde cualquier
// hilo — cada HandlerList protege sus propios registros con un mutex.
// Dicho esto, por convención (igual que el resto del tick loop) los
// eventos de juego deben dispararse desde el hilo principal: nada acá
// lo impide, pero Level/Player no son thread-safe por sí mismos, así
// que un callback que los toque debe seguir corriendo en el hilo
// principal.
#pragma once
#include "Event.h"
#include "EventPriority.h"
#include "HandlerList.h"
#include <typeindex>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <type_traits>
#include <utility>

namespace cursestone::event {

class EventManager {
public:
    static EventManager& get();

    // Registra `handler` para EventT en la prioridad dada. `owner` es
    // una etiqueta opaca (normalmente `this`) que solo sirve para que
    // unlistenAll(owner) pueda encontrar y quitar esta suscripción más
    // adelante — nunca se desreferencia. Devuelve un id de registro
    // utilizable con unlisten<EventT>().
    template <typename EventT>
    uint64_t listen(void* owner,
                     std::function<void(EventT&)> handler,
                     EventPriority priority = EventPriority::NORMAL,
                     bool ignoreCancelled = false) {
        static_assert(std::is_base_of<Event, EventT>::value,
                      "EventManager::listen<T>() requiere que T herede de event::Event");
        HandlerList& list = listFor(std::type_index(typeid(EventT)));
        return list.registerListener(owner, priority, ignoreCancelled,
            [handler](Event& e) { handler(static_cast<EventT&>(e)); });
    }

    // Quita un único registro de EventT por id.
    template <typename EventT>
    bool unlisten(uint64_t id) {
        static_assert(std::is_base_of<Event, EventT>::value,
                      "EventManager::unlisten<T>() requiere que T herede de event::Event");
        HandlerList* list = findList(std::type_index(typeid(EventT)));
        return list ? list->unregister(id) : false;
    }

    // Quita TODOS los registros (de cualquier tipo de evento) que
    // pertenezcan a `owner`. Llamar esto cuando un objeto Listener se
    // destruye, para que jamás quede un callback colgante apuntando a
    // memoria liberada.
    size_t unlistenAll(void* owner);

    // Despacha `event` a cada listener registrado para su tipo
    // DINÁMICO exacto, en orden de prioridad. No hace nada (aparte del
    // lookup) si nadie se registró jamás para ese tipo concreto.
    void call(Event& event);

    // Construye un EventT con `args`, lo despacha y lo devuelve por
    // valor para que el llamador pueda inspeccionar el resultado
    // (isCancelled(), campos reescritos por listeners, etc).
    template <typename EventT, typename... Args>
    EventT fire(Args&&... args) {
        static_assert(std::is_base_of<Event, EventT>::value,
                      "EventManager::fire<T>() requiere que T herede de event::Event");
        EventT ev(std::forward<Args>(args)...);
        call(ev);
        return ev;
    }

    // Cantidad de listeners registrados actualmente para EventT.
    // Principalmente para diagnóstico/tests.
    template <typename EventT>
    size_t listenerCount() const {
        HandlerList* list = findList(std::type_index(typeid(EventT)));
        return list ? list->size() : 0;
    }

private:
    HandlerList& listFor(const std::type_index& idx);
    HandlerList* findList(const std::type_index& idx) const;

    mutable std::mutex map_mtx_;
    std::unordered_map<std::type_index, std::unique_ptr<HandlerList>> lists_;
};

} // namespace cursestone::event
