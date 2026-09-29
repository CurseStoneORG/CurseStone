// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityCombustEvent.h — ported from cn.nukkit.event.entity.EntityCombustEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityCombustEvent : public EntityEvent, public Cancellable {
public:
    int seconds = 0;

    EntityCombustEvent(cursestone::entity::Entity* e, int s)
        : EntityEvent(e), seconds(s) {}

    const std::string& eventName() const override {
        static std::string n = "EntityCombustEvent"; return n;
    }
};
} // namespace
