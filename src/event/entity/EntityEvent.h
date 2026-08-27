// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityEvent.h — ported from cn.nukkit.event.entity.EntityEvent
#pragma once
#include "../Event.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityEvent : public Event {
public:
    atomikpe::entity::Entity* entity = nullptr;
    explicit EntityEvent(atomikpe::entity::Entity* e = nullptr) : entity(e) {}
    atomikpe::entity::Entity* getEntity() { return entity; }
    const std::string& eventName() const override {
        static std::string n = "EntityEvent"; return n;
    }
};
} // namespace
