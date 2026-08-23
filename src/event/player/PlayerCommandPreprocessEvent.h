// PlayerCommandPreprocessEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace cenisyspe { class Player; }
namespace cenisyspe::event::player {
class PlayerCommandPreprocessEvent : public Event, public Cancellable {
public:
    cenisyspe::Player* player;
    PlayerCommandPreprocessEvent(cenisyspe::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerCommandPreprocessEvent"; return n;
    }
};
} // namespace
