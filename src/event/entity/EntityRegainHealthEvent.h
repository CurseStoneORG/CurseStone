// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityRegainHealthEvent.h — ported from cn.nukkit.event.entity.EntityRegainHealthEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityRegainHealthEvent : public EntityEvent, public Cancellable {
public:
    float amount = 0.0f;
    int reason = 0;

    EntityRegainHealthEvent(atomikpe::entity::Entity* e, float a, int r)
        : EntityEvent(e), amount(a), reason(r) {}

    const std::string& eventName() const override {
        static std::string n = "EntityRegainHealthEvent"; return n;
    }
};
} // namespace
