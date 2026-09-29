// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityCombustByEntityEvent.h — ported from cn.nukkit.event.entity.EntityCombustByEntityEvent
#pragma once
#include "EntityCombustEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityCombustByEntityEvent : public EntityCombustEvent {
public:
    cursestone::entity::Entity* combustor = nullptr;

    EntityCombustByEntityEvent(cursestone::entity::Entity* e, int s, cursestone::entity::Entity* c)
        : EntityCombustEvent(e, s), combustor(c) {}

    const std::string& eventName() const override {
        static std::string n = "EntityCombustByEntityEvent"; return n;
    }
};
} // namespace
