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
// ExplosionPrimeEvent.h — ported from cn.nukkit.event.entity.ExplosionPrimeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace cursestone {
namespace entity {
class Entity;
}
}
namespace cursestone::event::entity {
class ExplosionPrimeEvent : public EntityEvent, public Cancellable {
  public:
    float radius = 0.0f;
    bool fireBreaking = false;

    ExplosionPrimeEvent(cursestone::entity::Entity* e, float r, bool fb)
        : EntityEvent(e), radius(r), fireBreaking(fb) {
    }

    const std::string& eventName() const override {
        static std::string n = "ExplosionPrimeEvent";
        return n;
    }
};
} // namespace
