// PlayerRespawnEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace mcppeserver { class Player; }
namespace mcppeserver::event::player {
class PlayerRespawnEvent : public Event, public Cancellable {
public:
    mcppeserver::Player* player;
    PlayerRespawnEvent(mcppeserver::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerRespawnEvent"; return n;
    }
};
} // namespace
