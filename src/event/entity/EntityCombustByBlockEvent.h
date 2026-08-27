// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityCombustByBlockEvent.h — ported from cn.nukkit.event.entity.EntityCombustByBlockEvent
#pragma once
#include "EntityCombustEvent.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityCombustByBlockEvent : public EntityCombustEvent {
public:
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;

    EntityCombustByBlockEvent(atomikpe::entity::Entity* e, int s, int bx, int by, int bz)
        : EntityCombustEvent(e, s), blockX(bx), blockY(by), blockZ(bz) {}

    const std::string& eventName() const override {
        static std::string n = "EntityCombustByBlockEvent"; return n;
    }
};
} // namespace
