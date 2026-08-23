// EntitySpawnEvent — entity event.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
namespace cenisyspe::entity { class Entity; }
namespace cenisyspe::event::entity {
class EntitySpawnEvent : public Event, public Cancellable {
public:
    cenisyspe::entity::Entity* entity;
    float amount = 0.0f;
    EntitySpawnEvent(cenisyspe::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntitySpawnEvent"; return n;
    }
};
} // namespace
