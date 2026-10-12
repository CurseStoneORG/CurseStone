// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDamageByEntityEvent.h — ported from cn.nukkit.event.entity.EntityDamageByEntityEvent
#pragma once
#include "EntityDamageEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityDamageByEntityEvent : public EntityDamageEvent {
public:
    cursestone::entity::Entity* damager = nullptr;

    EntityDamageByEntityEvent(cursestone::entity::Entity* e, float amount, cursestone::entity::Entity* d)
        : EntityDamageEvent(e, amount, DamageCause::ENTITY_ATTACK), damager(d) {
    }

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByEntityEvent"; return n;
    }
};
} // namespace
