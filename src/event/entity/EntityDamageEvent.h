// EntityDamageEvent — entity event.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace mcppeserver::entity { class Entity; }
namespace mcppeserver::event::entity {
class EntityDamageEvent : public Event, public Cancellable {
public:
    mcppeserver::entity::Entity* entity;
    float amount = 0.0f;
    EntityDamageEvent(mcppeserver::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntityDamageEvent"; return n;
    }
};
} // namespace
