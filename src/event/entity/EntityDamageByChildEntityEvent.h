// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDamageByChildEntityEvent.h — ported from cn.nukkit.event.entity.EntityDamageByChildEntityEvent
#pragma once
#include "EntityDamageByEntityEvent.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityDamageByChildEntityEvent : public EntityDamageByEntityEvent {
public:
    atomikpe::entity::Entity* child = nullptr;

    EntityDamageByChildEntityEvent(atomikpe::entity::Entity* e, float amount,
                                   atomikpe::entity::Entity* d, atomikpe::entity::Entity* c)
        : EntityDamageByEntityEvent(e, amount, d), child(c) {}

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByChildEntityEvent"; return n;
    }
};
} // namespace
