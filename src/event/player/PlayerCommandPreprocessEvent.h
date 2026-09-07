// PlayerCommandPreprocessEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace atomikpe { class Player; }
namespace atomikpe::event::player {
class PlayerCommandPreprocessEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    PlayerCommandPreprocessEvent(atomikpe::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerCommandPreprocessEvent"; return n;
    }
};
} // namespace
