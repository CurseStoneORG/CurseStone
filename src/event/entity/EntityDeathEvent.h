// EntityDeathEvent — entity event.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace cenisyspe::entity { class Entity; }
namespace cenisyspe::event::entity {
class EntityDeathEvent : public Event, public Cancellable {
public:
    cenisyspe::entity::Entity* entity;
    float amount = 0.0f;
    EntityDeathEvent(cenisyspe::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntityDeathEvent"; return n;
    }
};
} // namespace
