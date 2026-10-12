// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDamageByChildEntityEvent.h — ported from cn.nukkit.event.entity.EntityDamageByChildEntityEvent
#pragma once
#include "EntityDamageByEntityEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityDamageByChildEntityEvent : public EntityDamageByEntityEvent {
public:
    cursestone::entity::Entity* child = nullptr;

    EntityDamageByChildEntityEvent(cursestone::entity::Entity* e, float amount,
                                   cursestone::entity::Entity* d, cursestone::entity::Entity* c)
        : EntityDamageByEntityEvent(e, amount, d), child(c) {}

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByChildEntityEvent"; return n;
    }
};
} // namespace
