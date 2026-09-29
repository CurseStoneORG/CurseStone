// EntitySpawnEvent — entity event.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone::entity { class Entity; }
namespace cursestone::event::entity {
class EntitySpawnEvent : public Event, public Cancellable {
public:
    cursestone::entity::Entity* entity;
    float amount = 0.0f;
    EntitySpawnEvent(cursestone::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntitySpawnEvent"; return n;
    }
};
} // namespace
