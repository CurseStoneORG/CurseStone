// PlayerLoginEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace atomikpe { class Player; }
namespace atomikpe::event::player {
class PlayerLoginEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    PlayerLoginEvent(atomikpe::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerLoginEvent"; return n;
    }
};
} // namespace
