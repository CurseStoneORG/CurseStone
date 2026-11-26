/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerHungerChangeEvent.h — fired when a player's hunger (food) level
// is about to move from one value to another, whoever asked for it:
// eating, the exhaustion drain crossing a saturation point, the
// saturation potion effect, the creative-mode resync, data restore
// during login, or a respawn reset.
//
// Hunger, saturation and exhaustion are three DIFFERENT values: hunger
// is the 0..20 drumstick bar this event carries, saturation is the
// invisible reserve drained first, exhaustion is the accumulator that
// pushes saturation/food down. Exhaustion increments never fire this
// event directly — only the food-point change they eventually cause
// does (with reason EXHAUSTION).
//
// Timing: before the new level is applied. Listeners may rewrite the
// new level (clamped to the valid 0..20 range by the caller) or cancel
// the whole change to keep the previous level. Re-entrancy is guarded:
// a listener that triggers another hunger change updates the value
// silently instead of dispatching a nested event, so event storms and
// infinite loops are impossible.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

// Why the food level is moving. Hoisted to namespace scope (aliased inside
// the event as Reason) so Player.h and plugin code can name it in their
// own signatures without dragging the whole event type along.
enum class HungerChangeReason : uint8_t {
    EATING = 0,             // Food consumed.
    EXHAUSTION = 1,         // Exhaustion accumulator drained it.
    SATURATION_EFFECT = 2,  // Saturation potion effect.
    CREATIVE_SYNC = 3,      // Creative-mode refill.
    LOGIN = 4,              // Saved data restored at login.
    RESPAWN = 5,            // Death respawn reset.
    PLUGIN = 6,             // Plugin or command code path.
    UNKNOWN = 7,            // Unidentified server-internal change.
    HUNGER_EFFECT = 8,      // Hunger potion effect drain.
};

class PlayerHungerChangeEvent : public PlayerEvent, public Cancellable {
  public:
    using Reason = HungerChangeReason;

    PlayerHungerChangeEvent(cursestone::Player* p, int32_t oldLevel, int32_t newLevel,
                            Reason reason)
        : PlayerEvent(p), old_level_(oldLevel), new_level_(newLevel), reason_(reason) {
    }

    // Food level before the change (0..20).
    int32_t getOldLevel() const {
        return old_level_;
    }
    // Food level requested by the caller. Writable; the caller clamps
    // the final value into the valid range before applying it.
    int32_t getNewLevel() const {
        return new_level_;
    }
    void setNewLevel(int32_t level) {
        new_level_ = level;
    }
    // What asked for the change (see Reason).
    Reason reason() const {
        return reason_;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerHungerChangeEvent";
        return n;
    }

    // Human-readable reason word shared by the plugin bindings.
    static const char* reasonName(HungerChangeReason reason) {
        switch (reason) {
        case Reason::EATING:
            return "eating";
        case Reason::EXHAUSTION:
            return "exhaustion";
        case Reason::SATURATION_EFFECT:
            return "saturation_effect";
        case Reason::CREATIVE_SYNC:
            return "creative_sync";
        case Reason::LOGIN:
            return "login";
        case Reason::RESPAWN:
            return "respawn";
        case Reason::PLUGIN:
            return "plugin";
        default:
            return "unknown";
        }
    }

  private:
    int32_t old_level_;
    int32_t new_level_;
    HungerChangeReason reason_;
};

} // namespace cursestone::event::player
