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
// EntityExplodeEvent.h — ported from cn.nukkit.event.entity.EntityExplodeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include <vector>
#include <tuple>
#include <string>
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityExplodeEvent : public EntityEvent, public Cancellable {
public:
    int x = 0;
    int y = 0;
    int z = 0;
    std::vector<std::tuple<int,int,int>> blocks;
    float yield = 0.0f;

    EntityExplodeEvent(cursestone::entity::Entity* e, int bx, int by, int bz,
                        std::vector<std::tuple<int,int,int>> bl, float yld)
        : EntityEvent(e), x(bx), y(by), z(bz), blocks(std::move(bl)), yield(yld) {}

    const std::string& eventName() const override {
        static std::string n = "EntityExplodeEvent"; return n;
    }
};
} // namespace
