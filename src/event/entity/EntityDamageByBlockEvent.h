// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDamageByBlockEvent.h — ported from cn.nukkit.event.entity.EntityDamageByBlockEvent
#pragma once
#include "EntityDamageEvent.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityDamageByBlockEvent : public EntityDamageEvent {
public:
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;

    EntityDamageByBlockEvent(atomikpe::entity::Entity* e, float amount, int bx, int by, int bz)
        : EntityDamageEvent(e) {
        this->amount = amount;
        blockX = bx; blockY = by; blockZ = bz;
    }

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByBlockEvent"; return n;
    }
};
} // namespace
