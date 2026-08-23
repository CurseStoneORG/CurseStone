// PlayerMoveEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace cenisyspe { class Player; }
namespace cenisyspe::event::player {
class PlayerMoveEvent : public Event, public Cancellable {
public:
    cenisyspe::Player* player;
    PlayerMoveEvent(cenisyspe::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerMoveEvent"; return n;
    }
};
} // namespace
