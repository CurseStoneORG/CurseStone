// PlayerQuitEvent — se dispara cuando un jugador se desconecta.
// `message` es el mensaje de broadcast ("Steve left the game"); un
// listener puede reescribirlo o vaciarlo para silenciarlo.
//
// Cancelar el evento solo suprime el mensaje de broadcast — para
// cuando este evento se dispara la desconexion ya esta en curso y no
// puede revertirse.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace atomikpe { class Player; }
namespace atomikpe::event::player {
class PlayerQuitEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    std::string message;
    PlayerQuitEvent(atomikpe::Player* p = nullptr, std::string msg = "")
        : player(p), message(std::move(msg)) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerQuitEvent"; return n;
    }
};
} // namespace
