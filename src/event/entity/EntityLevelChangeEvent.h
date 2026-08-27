// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityLevelChangeEvent.h — ported from cn.nukkit.event.entity.EntityLevelChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe { namespace level { class Level; } }
namespace atomikpe::event::entity {
class EntityLevelChangeEvent : public EntityEvent, public Cancellable {
public:
    atomikpe::level::Level* level = nullptr;
    int oldLevelId = 0;
    int newLevelId = 0;

    EntityLevelChangeEvent(atomikpe::entity::Entity* e, atomikpe::level::Level* lvl, int oldId, int newId)
        : EntityEvent(e), level(lvl), oldLevelId(oldId), newLevelId(newId) {}

    const std::string& eventName() const override {
        static std::string n = "EntityLevelChangeEvent"; return n;
    }
};
} // namespace
