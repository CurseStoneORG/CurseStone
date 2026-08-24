// EntityDeathEvent — entity event.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace atomikpe::entity { class Entity; }
namespace atomikpe::event::entity {
class EntityDeathEvent : public Event, public Cancellable {
public:
    atomikpe::entity::Entity* entity;
    float amount = 0.0f;
    EntityDeathEvent(atomikpe::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntityDeathEvent"; return n;
    }
};
} // namespace
