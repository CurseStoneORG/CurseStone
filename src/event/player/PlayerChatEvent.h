// PlayerChatEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace atomikpe { class Player; }
namespace atomikpe::event::player {
class PlayerChatEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    PlayerChatEvent(atomikpe::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerChatEvent"; return n;
    }
};
} // namespace
