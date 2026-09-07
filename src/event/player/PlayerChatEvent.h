// PlayerChatEvent — se dispara cuando un jugador envia un mensaje de
// chat (no un comando). `message` es el texto a difundir; un listener
// puede reescribirlo (por ejemplo para anteponer un prefijo de rango)
// antes de que se difunda a los demas jugadores. Cancelar el evento
// evita el broadcast por completo.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace atomikpe { class Player; }
namespace atomikpe::event::player {
class PlayerChatEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    std::string message;
    PlayerChatEvent(atomikpe::Player* p = nullptr, std::string msg = "")
        : player(p), message(std::move(msg)) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerChatEvent"; return n;
    }
};
} // namespace
