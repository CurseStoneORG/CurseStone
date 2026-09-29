// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityLevelChangeEvent.h — ported from cn.nukkit.event.entity.EntityLevelChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone { namespace level { class Level; } }
namespace cursestone::event::entity {
class EntityLevelChangeEvent : public EntityEvent, public Cancellable {
public:
    cursestone::level::Level* level = nullptr;
    int oldLevelId = 0;
    int newLevelId = 0;

    EntityLevelChangeEvent(cursestone::entity::Entity* e, cursestone::level::Level* lvl, int oldId, int newId)
        : EntityEvent(e), level(lvl), oldLevelId(oldId), newLevelId(newId) {}

    const std::string& eventName() const override {
        static std::string n = "EntityLevelChangeEvent"; return n;
    }
};
} // namespace
