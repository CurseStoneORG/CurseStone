// PlayerQuitEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace mcppeserver { class Player; }
namespace mcppeserver::event::player {
class PlayerQuitEvent : public Event, public Cancellable {
public:
    mcppeserver::Player* player;
    PlayerQuitEvent(mcppeserver::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerQuitEvent"; return n;
    }
};
} // namespace
