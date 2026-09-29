// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerAchievementAwardedEvent.h — ported from cn.nukkit.event.player.PlayerAchievementAwardedEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerAchievementAwardedEvent : public PlayerEvent, public Cancellable {
public:
    std::string achievement;
    PlayerAchievementAwardedEvent(cursestone::Player* p, const std::string& ach)
        : PlayerEvent(p), achievement(ach) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerAchievementAwardedEvent"; return n;
    }
};
} // namespace cursestone::event::player
