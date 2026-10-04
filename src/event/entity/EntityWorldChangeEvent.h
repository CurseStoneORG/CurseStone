// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityLevelChangeEvent.h — ported from cn.nukkit.event.entity.EntityLevelChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone { namespace world { class World; } }
namespace cursestone::event::entity {
class EntityLevelChangeEvent : public EntityEvent, public Cancellable {
public:
    cursestone::world::World* world = nullptr;
    int oldWorldId = 0;
    int newWorldId = 0;

    EntityLevelChangeEvent(cursestone::entity::Entity* e, cursestone::world::World* w, int oldId, int newId)
        : EntityEvent(e), world(w), oldWorldId(oldId), newWorldId(newId) {}

    const std::string& eventName() const override {
        static std::string n = "EntityLevelChangeEvent"; return n;
    }
};
} // namespace
