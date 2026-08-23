// PlayerLoginEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace cenisyspe { class Player; }
namespace cenisyspe::event::player {
class PlayerLoginEvent : public Event, public Cancellable {
public:
    cenisyspe::Player* player;
    PlayerLoginEvent(cenisyspe::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerLoginEvent"; return n;
    }
};
} // namespace
