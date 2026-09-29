// PlayerDeathEvent — se dispara cuando la salud de un jugador llega a
// 0. `message` es el mensaje de broadcast ("Steve fue asesinado por
// Alex"); un listener puede reescribirlo o vaciarlo para silenciarlo.
//
// Cancelar el evento solo suprime el mensaje de broadcast — la
// muerte en si (salud a 0) ya ocurrio y no puede revertirse desde
// aqui. Esto mirrors el mismo contrato que PlayerJoinEvent/
// PlayerQuitEvent: no hay forma de "des-morir" a alguien post-hoc,
// solo de callar el aviso.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone { class Player; }
namespace cursestone::event::player {
class PlayerDeathEvent : public Event, public Cancellable {
public:
    cursestone::Player* player;
    std::string message;
    PlayerDeathEvent(cursestone::Player* p = nullptr, std::string msg = "")
        : player(p), message(std::move(msg)) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerDeathEvent"; return n;
    }
};
} // namespace
