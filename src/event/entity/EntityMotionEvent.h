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
// EntityMotionEvent.h — ported from cn.nukkit.event.entity.EntityMotionEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace cursestone {
namespace entity {
class Entity;
}
}
namespace cursestone::event::entity {
class EntityMotionEvent : public EntityEvent, public Cancellable {
  public:
    double motX = 0.0;
    double motY = 0.0;
    double motZ = 0.0;

    EntityMotionEvent(cursestone::entity::Entity* e, double mx, double my, double mz)
        : EntityEvent(e), motX(mx), motY(my), motZ(mz) {
    }

    const std::string& eventName() const override {
        static std::string n = "EntityMotionEvent";
        return n;
    }
};
} // namespace
