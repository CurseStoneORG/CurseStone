// EntitySpawnEvent — entity event.
#pragma once
#include "Event.h"
#include "Cancellable.h"
namespace mcppeserver::entity { class Entity; }
namespace mcppeserver::event::entity {
class EntitySpawnEvent : public Event, public Cancellable {
public:
    mcppeserver::entity::Entity* entity;
    float amount = 0.0f;
    EntitySpawnEvent(mcppeserver::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntitySpawnEvent"; return n;
    }
};
} // namespace
