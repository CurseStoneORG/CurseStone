// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityEvent.h — ported from cn.nukkit.event.entity.EntityEvent
#pragma once
#include "../Event.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityEvent : public Event {
public:
    cursestone::entity::Entity* entity = nullptr;
    explicit EntityEvent(cursestone::entity::Entity* e = nullptr) : entity(e) {}
    cursestone::entity::Entity* getEntity() { return entity; }
    const std::string& eventName() const override {
        static std::string n = "EntityEvent"; return n;
    }
};
} // namespace
