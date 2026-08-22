// PlayerLoginEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace mcppeserver { class Player; }
namespace mcppeserver::event::player {
class PlayerLoginEvent : public Event, public Cancellable {
public:
    mcppeserver::Player* player;
    PlayerLoginEvent(mcppeserver::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerLoginEvent"; return n;
    }
};
} // namespace
