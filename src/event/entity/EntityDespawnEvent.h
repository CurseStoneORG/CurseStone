// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityDespawnEvent.h — ported from cn.nukkit.event.entity.EntityDespawnEvent
#pragma once
#include "EntityEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityDespawnEvent : public EntityEvent {
public:
    explicit EntityDespawnEvent(cursestone::entity::Entity* e) : EntityEvent(e) {}

    const std::string& eventName() const override {
        static std::string n = "EntityDespawnEvent"; return n;
    }
};
} // namespace
