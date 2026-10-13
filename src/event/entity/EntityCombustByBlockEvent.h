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
// EntityCombustByBlockEvent.h — ported from cn.nukkit.event.entity.EntityCombustByBlockEvent
#pragma once
#include "EntityCombustEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityCombustByBlockEvent : public EntityCombustEvent {
public:
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;

    EntityCombustByBlockEvent(cursestone::entity::Entity* e, int s, int bx, int by, int bz)
        : EntityCombustEvent(e, s), blockX(bx), blockY(by), blockZ(bz) {}

    const std::string& eventName() const override {
        static std::string n = "EntityCombustByBlockEvent"; return n;
    }
};
} // namespace
