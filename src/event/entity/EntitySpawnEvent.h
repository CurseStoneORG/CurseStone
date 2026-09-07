// EntitySpawnEvent — entity event.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace atomikpe::entity { class Entity; }
namespace atomikpe::event::entity {
class EntitySpawnEvent : public Event, public Cancellable {
public:
    atomikpe::entity::Entity* entity;
    float amount = 0.0f;
    EntitySpawnEvent(atomikpe::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntitySpawnEvent"; return n;
    }
};
} // namespace
