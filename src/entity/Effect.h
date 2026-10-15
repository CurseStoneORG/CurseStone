/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// Effect — potion effect flyweight (speed, slowness, jump boost, etc.).
#pragma once
#include <cstdint>
#include <string>
namespace cursestone::entity {
enum EffectId : uint8_t {
    EFF_SPEED = 1,
    EFF_SLOWNESS = 2,
    EFF_HASTE = 3,
    EFF_MINING_FATIGUE = 4,
    EFF_STRENGTH = 5,
    EFF_INSTANT_HEALTH = 6,
    EFF_INSTANT_DAMAGE = 7,
    EFF_JUMP = 8,
    EFF_NAUSEA = 9,
    EFF_REGENERATION = 10,
    EFF_RESISTANCE = 11,
    EFF_FIRE_RESISTANCE = 12,
    EFF_WATER_BREATHING = 13,
    EFF_INVISIBILITY = 14,
    EFF_BLINDNESS = 15,
    EFF_NIGHT_VISION = 16,
    EFF_HUNGER = 17,
    EFF_WEAKNESS = 18,
    EFF_POISON = 19,
    EFF_WITHER = 20,
    EFF_HEALTH_BOOST = 21,
    EFF_ABSORPTION = 22,
    EFF_SATURATION = 23,
};
class Effect {
  public:
    Effect(EffectId id, std::string name, int duration_ticks, int amplifier)
        : id_(id), name_(std::move(name)), duration_(duration_ticks), amplifier_(amplifier) {
    }
    EffectId id() const {
        return id_;
    }
    const std::string& name() const {
        return name_;
    }
    int duration() const {
        return duration_;
    }
    int amplifier() const {
        return amplifier_;
    }
    void tick() {
        if (duration_ > 0)
            --duration_;
    }
    bool isExpired() const {
        return duration_ <= 0;
    }

  private:
    EffectId id_;
    std::string name_;
    int duration_;
    int amplifier_;
};
} // namespace
