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
// EntityDamageByChildEntityEvent.h — ported from cn.nukkit.event.entity.EntityDamageByChildEntityEvent
#pragma once
#include "EntityDamageByEntityEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityDamageByChildEntityEvent : public EntityDamageByEntityEvent {
public:
    cursestone::entity::Entity* child = nullptr;

    EntityDamageByChildEntityEvent(cursestone::entity::Entity* e, float amount,
                                   cursestone::entity::Entity* d, cursestone::entity::Entity* c)
        : EntityDamageByEntityEvent(e, amount, d), child(c) {}

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByChildEntityEvent"; return n;
    }
};
} // namespace
