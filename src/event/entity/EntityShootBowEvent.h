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
// EntityShootBowEvent.h — ported from cn.nukkit.event.entity.EntityShootBowEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityShootBowEvent : public EntityEvent, public Cancellable {
public:
    cursestone::item::ItemStack bow;
    cursestone::entity::Entity* projectile = nullptr;
    float force = 0.0f;

    EntityShootBowEvent(cursestone::entity::Entity* e,
                        const cursestone::item::ItemStack& b,
                        cursestone::entity::Entity* p, float f)
        : EntityEvent(e), bow(b), projectile(p), force(f) {}

    const std::string& eventName() const override {
        static std::string n = "EntityShootBowEvent"; return n;
    }
};
} // namespace
