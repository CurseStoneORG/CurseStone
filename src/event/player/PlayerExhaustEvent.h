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
// PlayerExhaustEvent.h — fired BEFORE a player's exhaustion accumulator
// is increased, for every gameplay action the server tracks: attacking,
// taking damage, mining, health regeneration, hunger potions, sprinting
// movement and swimming.
//
// Exhaustion is its own mechanic — NOT hunger and NOT saturation. When
// the accumulator crosses the 4.0 threshold the server drains one point
// of saturation (or food when saturation is empty); that later hunger
// change fires PlayerHungerChangeEvent, never this one.
//
// Timing: the event runs before the accumulator moves, so cancelling it
// drops the whole increment and the amount can be rewritten (finite,
// non-negative values only; anything else is treated as no-op by the
// caller).
//
// Causes are small integers so they can be transported over the plugin
// APIs. CAUSE_FLAG_SPRINT is a bitwise flag, never a standalone cause:
// it is OR-ed onto a base cause (CAUSE_WALKING | CAUSE_FLAG_SPRINT is
// how sprint movement reports itself) and read back through
// hasSprintFlag() / baseCause().
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <cmath>
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerExhaustEvent : public PlayerEvent, public Cancellable {
  public:
    // Exhaustion causes. Values are part of the plugin-facing surface.
    static constexpr int32_t CAUSE_ATTACK = 1;         // Attacking an entity.
    static constexpr int32_t CAUSE_DAMAGE = 2;         // Receiving damage.
    static constexpr int32_t CAUSE_MINING = 3;         // Breaking blocks.
    static constexpr int32_t CAUSE_HEALTH_REGEN = 4;   // Regenerating health.
    static constexpr int32_t CAUSE_POTION = 5;         // Hunger potion effect.
    static constexpr int32_t CAUSE_WALKING = 6;        // Walking.
    static constexpr int32_t CAUSE_SNEAKING = 7;       // Sneaking movement.
    static constexpr int32_t CAUSE_SWIMMING = 8;       // Swimming.
    static constexpr int32_t CAUSE_JUMPING = 10;       // Jumping.
    static constexpr int32_t CAUSE_CUSTOM = 11;        // Plugin / internal custom source.
    // Bitwise flag OR-ed onto a base cause when the sprint modifier
    // applied. (CAUSE_WALKING | CAUSE_FLAG_SPRINT).
    static constexpr int32_t CAUSE_FLAG_SPRINT = 0x10000;

    PlayerExhaustEvent(cursestone::Player* p, float amount, int32_t cause)
        : PlayerEvent(p), amount_(amount), cause_(cause) {
    }

    // Exhaustion units the caller wants to add. Writable: the caller
    // applies whatever value survives the listeners (already validated).
    float amount() const {
        return amount_;
    }
    void setAmount(float amount) {
        amount_ = std::isfinite(amount) && amount > 0.0f ? amount : 0.0f;
    }

    // Raw cause word: a base cause, optionally carrying CAUSE_FLAG_SPRINT.
    int32_t cause() const {
        return cause_;
    }
    // The cause without flag bits (CAUSE_WALKING, CAUSE_SWIMMING, ...).
    int32_t baseCause() const {
        return cause_ & 0xFFFF;
    }
    // True when the sprint modifier flag is present on the cause word.
    bool hasSprintFlag() const {
        return (cause_ & CAUSE_FLAG_SPRINT) != 0;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerExhaustEvent";
        return n;
    }

    // Human-readable cause word shared by the plugin bindings: the base
    // cause name plus a "|sprint" suffix when the sprint flag rides on
    // it ("walking|sprint", "swimming", "mining", ...).
    static std::string causeName(int32_t cause) {
        const char* base = "custom";
        switch (cause & 0xFFFF) {
        case CAUSE_ATTACK:
            base = "attack";
            break;
        case CAUSE_DAMAGE:
            base = "damage";
            break;
        case CAUSE_MINING:
            base = "mining";
            break;
        case CAUSE_HEALTH_REGEN:
            base = "health_regen";
            break;
        case CAUSE_POTION:
            base = "potion";
            break;
        case CAUSE_WALKING:
            base = "walking";
            break;
        case CAUSE_SNEAKING:
            base = "sneaking";
            break;
        case CAUSE_SWIMMING:
            base = "swimming";
            break;
        case CAUSE_JUMPING:
            base = "jumping";
            break;
        default:
            break;
        }
        std::string s = base;
        if (cause & CAUSE_FLAG_SPRINT)
            s += "|sprint";
        return s;
    }

  private:
    float amount_;
    int32_t cause_;
};

} // namespace cursestone::event::player
