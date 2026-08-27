// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityShootBowEvent.h — ported from cn.nukkit.event.entity.EntityShootBowEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityShootBowEvent : public EntityEvent, public Cancellable {
public:
    atomikpe::item::ItemStack bow;
    atomikpe::entity::Entity* projectile = nullptr;
    float force = 0.0f;

    EntityShootBowEvent(atomikpe::entity::Entity* e,
                        const atomikpe::item::ItemStack& b,
                        atomikpe::entity::Entity* p, float f)
        : EntityEvent(e), bow(b), projectile(p), force(f) {}

    const std::string& eventName() const override {
        static std::string n = "EntityShootBowEvent"; return n;
    }
};
} // namespace
