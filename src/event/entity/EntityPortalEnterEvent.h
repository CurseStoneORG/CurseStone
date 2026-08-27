// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityPortalEnterEvent.h — ported from cn.nukkit.event.entity.EntityPortalEnterEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityPortalEnterEvent : public EntityEvent, public Cancellable {
public:
    int dimension = 0;

    EntityPortalEnterEvent(atomikpe::entity::Entity* e, int dim)
        : EntityEvent(e), dimension(dim) {}

    const std::string& eventName() const override {
        static std::string n = "EntityPortalEnterEvent"; return n;
    }
};
} // namespace
