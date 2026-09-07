// PlayerJoinEvent — se dispara justo despues de que un jugador termina
// de spawnear en el mundo. `message` es el mensaje de broadcast
// ("Steve joined the game"); un listener puede reescribirlo, o
// vaciarlo para silenciar el aviso por completo.
//
// Cancelar el evento solo suprime el mensaje de broadcast — el
// jugador ya esta conectado y spawneado para cuando este evento se
// dispara, asi que cancelar no puede rechazar la conexion en si.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace atomikpe { class Player; }
namespace atomikpe::event::player {
class PlayerJoinEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    std::string message;
    PlayerJoinEvent(atomikpe::Player* p = nullptr, std::string msg = "")
        : player(p), message(std::move(msg)) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerJoinEvent"; return n;
    }
};
} // namespace
