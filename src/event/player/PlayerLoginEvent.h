// PlayerLoginEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone { class Player; }
namespace cursestone::event::player {
class PlayerLoginEvent : public Event, public Cancellable {
public:
    cursestone::Player* player;
    PlayerLoginEvent(cursestone::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerLoginEvent"; return n;
    }
};
} // namespace
