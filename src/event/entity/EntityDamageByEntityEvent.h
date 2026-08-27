// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDamageByEntityEvent.h — ported from cn.nukkit.event.entity.EntityDamageByEntityEvent
#pragma once
#include "EntityDamageEvent.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityDamageByEntityEvent : public EntityDamageEvent {
public:
    atomikpe::entity::Entity* damager = nullptr;

    EntityDamageByEntityEvent(atomikpe::entity::Entity* e, float amount, atomikpe::entity::Entity* d)
        : EntityDamageEvent(e), damager(d) {
        this->amount = amount;
    }

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByEntityEvent"; return n;
    }
};
} // namespace
