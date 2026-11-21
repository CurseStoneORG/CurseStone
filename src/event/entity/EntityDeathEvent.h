/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDeathEvent.h — fired when a world mob's health reaches zero, after
// the lethal damage was applied: the death animation, the vanilla drop
// table and the delayed record removal already ran or are committed.
// Not cancellable — for that, cancel the EntityDamage(ByEntity)Event that
// carries the lethal hit. Player deaths fire PlayerDeathEvent instead.
#pragma once
#include "../Event.h"
#include "EntityDamageEvent.h"
#include <string>

namespace cursestone {
namespace entity {
class Entity;
}
}

namespace cursestone::event::entity {

class EntityDeathEvent : public Event {
  public:
    // Mob victims are world records without a C++ Entity object (the same
    // identity shape the mob damage events use).
    int64_t victimEid = 0;
    int32_t victimType = 0;
    DamageCause cause = DamageCause::CUSTOM;
    // Killer of the lethal hit (a Player for melee/projectile deaths) or
    // nullptr when the killer blow had no attacker (plugin health writes).
    cursestone::entity::Entity* damager = nullptr;

    EntityDeathEvent(int64_t eid, int32_t type, DamageCause c,
                     cursestone::entity::Entity* d = nullptr)
        : victimEid(eid), victimType(type), cause(c), damager(d) {
    }

    const std::string& eventName() const override {
        static std::string n = "EntityDeathEvent";
        return n;
    }
};

} // namespace cursestone::event::entity
