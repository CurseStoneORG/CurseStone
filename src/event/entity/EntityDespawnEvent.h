// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDespawnEvent.h — ported from cn.nukkit.event.entity.EntityDespawnEvent
#pragma once
#include "EntityEvent.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityDespawnEvent : public EntityEvent {
public:
    explicit EntityDespawnEvent(atomikpe::entity::Entity* e) : EntityEvent(e) {}

    const std::string& eventName() const override {
        static std::string n = "EntityDespawnEvent"; return n;
    }
};
} // namespace
