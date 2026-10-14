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
// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDamageByEntityEvent.h — ported from cn.nukkit.event.entity.EntityDamageByEntityEvent
#pragma once
#include "EntityDamageEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityDamageByEntityEvent : public EntityDamageEvent {
public:
    cursestone::entity::Entity* damager = nullptr;

    // `cause` defaults to ENTITY_ATTACK (melee). World::tickArrows passes
    // DamageCause::PROJECTILE so Lua listeners can tell bow hits from
    // punches (PMMP keeps the cause from the source event too).
    EntityDamageByEntityEvent(cursestone::entity::Entity* e, float amount,
                              cursestone::entity::Entity* d,
                              DamageCause c = DamageCause::ENTITY_ATTACK)
        : EntityDamageEvent(e, amount, c), damager(d) {
    }

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByEntityEvent"; return n;
    }
};
} // namespace
