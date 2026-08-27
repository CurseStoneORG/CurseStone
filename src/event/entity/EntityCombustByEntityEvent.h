// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityCombustByEntityEvent.h — ported from cn.nukkit.event.entity.EntityCombustByEntityEvent
#pragma once
#include "EntityCombustEvent.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityCombustByEntityEvent : public EntityCombustEvent {
public:
    atomikpe::entity::Entity* combustor = nullptr;

    EntityCombustByEntityEvent(atomikpe::entity::Entity* e, int s, atomikpe::entity::Entity* c)
        : EntityCombustEvent(e, s), combustor(c) {}

    const std::string& eventName() const override {
        static std::string n = "EntityCombustByEntityEvent"; return n;
    }
};
} // namespace
