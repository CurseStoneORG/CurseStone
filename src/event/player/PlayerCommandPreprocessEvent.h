// PlayerCommandPreprocessEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include "Cancellable.h"
namespace mcppeserver { class Player; }
namespace mcppeserver::event::player {
class PlayerCommandPreprocessEvent : public Event, public Cancellable {
public:
    mcppeserver::Player* player;
    PlayerCommandPreprocessEvent(mcppeserver::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerCommandPreprocessEvent"; return n;
    }
};
} // namespace
