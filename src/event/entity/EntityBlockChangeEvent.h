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
// EntityBlockChangeEvent.h — ported from cn.nukkit.event.entity.EntityBlockChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityBlockChangeEvent : public EntityEvent, public Cancellable {
public:
    int x = 0;
    int y = 0;
    int z = 0;
    uint8_t blockId = 0;
    uint8_t blockData = 0;

    EntityBlockChangeEvent(cursestone::entity::Entity* e, int bx, int by, int bz, uint8_t id, uint8_t data)
        : EntityEvent(e), x(bx), y(by), z(bz), blockId(id), blockData(data) {}

    const std::string& eventName() const override {
        static std::string n = "EntityBlockChangeEvent"; return n;
    }
};
} // namespace
