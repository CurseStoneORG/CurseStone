// EntityDamageEvent — entity event.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace atomikpe::entity { class Entity; }
namespace atomikpe::event::entity {
class EntityDamageEvent : public Event, public Cancellable {
public:
    atomikpe::entity::Entity* entity;
    float amount = 0.0f;
    EntityDamageEvent(atomikpe::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntityDamageEvent"; return n;
    }
};
} // namespace
