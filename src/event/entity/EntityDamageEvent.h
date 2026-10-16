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
#pragma once

#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"

namespace cursestone::entity {
class Entity;
}

namespace cursestone::event::entity {

// Fired before damage is applied; listeners can modify the amount or cancel it.
enum class DamageCause {
    CUSTOM = 0,
    ENTITY_ATTACK = 1,
    CONTACT = 2,     // Contact damage, such as touching fire.
    FALL = 3,        // Fall damage.
    LAVA = 4,        // Lava contact.
    FIRE = 5,        // Contact with fire.
    FIRE_TICK = 6,   // Ongoing fire damage.
    DROWNING = 7,    // Suffocation underwater.
    CACTUS = 8,      // Cactus contact.
    STARVATION = 9,  // Damage caused by an empty hunger bar.
    VOID = 10,       // Falling into the void.
    PROJECTILE = 11, // Projectile damage; used as a fallback if the shooter is offline.
    MAGIC = 12,      // Potion damage, such as poison, wither, or harming.
};

class EntityDamageEvent : public Event, public Cancellable {
  public:
    cursestone::entity::Entity* entity;
    float amount = 0.0f;
    DamageCause cause = DamageCause::CUSTOM;

    EntityDamageEvent(cursestone::entity::Entity* e = nullptr, float amt = 0.0f,
                      DamageCause c = DamageCause::CUSTOM)
        : entity(e), amount(amt), cause(c) {
    }

    const std::string& eventName() const override {
        static std::string n = "EntityDamageEvent";
        return n;
    }
};

} // namespace cursestone::event::entity