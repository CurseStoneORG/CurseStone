// PlayerCommandPreprocessEvent — player event. Fires when the corresponding action occurs.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone { class Player; }
namespace cursestone::event::player {
class PlayerCommandPreprocessEvent : public Event, public Cancellable {
public:
    cursestone::Player* player;
    PlayerCommandPreprocessEvent(cursestone::Player* p = nullptr) : player(p) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerCommandPreprocessEvent"; return n;
    }
};
} // namespace
