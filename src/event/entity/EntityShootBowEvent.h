// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityShootBowEvent.h — ported from cn.nukkit.event.entity.EntityShootBowEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityShootBowEvent : public EntityEvent, public Cancellable {
public:
    cursestone::item::ItemStack bow;
    cursestone::entity::Entity* projectile = nullptr;
    float force = 0.0f;

    EntityShootBowEvent(cursestone::entity::Entity* e,
                        const cursestone::item::ItemStack& b,
                        cursestone::entity::Entity* p, float f)
        : EntityEvent(e), bow(b), projectile(p), force(f) {}

    const std::string& eventName() const override {
        static std::string n = "EntityShootBowEvent"; return n;
    }
};
} // namespace
