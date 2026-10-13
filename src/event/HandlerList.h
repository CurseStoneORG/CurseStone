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
// CONTRATO DE ESTABILIDAD (esto reemplaza una versión anterior que
// nunca llegó a conectarse al resto del código y tenía dos bugs
// latentes: dispatch() recibía el evento como `const Event&`, por lo
// que ningún listener podía llamar jamás a setCancelled(); y los
// campos priority/ignoreCancelled de EventHandler.h no se consultaban
// en ningún lado):
//
//   - dispatch() recibe una Event& NO const, para que los listeners
//     puedan leer Y modificar el evento (cancelarlo, reescribir un
//     mensaje de chat, etc).
//   - Los listeners corren en orden de EventPriority (LOWEST primero,
//     MONITOR último), así que las prioridades altas ven el resultado
//     de las bajas, y los listeners MONITOR siempre ven el estado final.
//   - Un listener registrado con ignoreCancelled=true se salta en
//     cuanto el evento ya fue cancelado por un listener anterior —
//     replica RegisteredListener::callEvent() de PMMP.
//   - Un listener que lanza una excepción se captura, se loguea y se
//     salta: un handler roto jamás puede tumbar el tick loop ni
//     bloquear al resto de los handlers (misma doctrina de estabilidad
//     que ServerScheduler post-incidente del fire-cascade).
//   - Todas las mutaciones (registrar/desregistrar) están protegidas
//     por mutex, así que puede usarse desde cualquier hilo. dispatch()
//     toma una copia (snapshot) bajo el lock y ejecuta los callbacks
//     fuera de él, para que un handler pueda registrar/desregistrar
//     listeners (incluso para este mismo tipo de evento) sin causar
//     un deadlock contra sí mismo.
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
    // está vacío (nada que registrar).
    uint64_t registerListener(void* owner, EventPriority priority,
                               bool ignoreCancelled,
                               std::function<void(Event&)> callback);

    // Elimina un único registro por id. true si se encontró.
    bool unregister(uint64_t id);

    // Elimina todos los registros cuyo `owner` coincida. Devuelve
    // cuántos se eliminaron. Se usa cuando un Listener se destruye.
    size_t unregisterOwner(void* owner);

    // Elimina absolutamente todos los registros.
    void unregisterAll();

    // Ejecuta cada listener registrado para `event`, en orden de
    // prioridad, saltando los ignoreCancelled una vez el evento está
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
