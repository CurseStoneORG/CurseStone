// EntityDeathEvent — entity event.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone::entity { class Entity; }
namespace cursestone::event::entity {
class EntityDeathEvent : public Event, public Cancellable {
public:
    cursestone::entity::Entity* entity;
    float amount = 0.0f;
    EntityDeathEvent(cursestone::entity::Entity* e = nullptr) : entity(e) {}
    const std::string& eventName() const override {
        static std::string n = "EntityDeathEvent"; return n;
    }
};
} // namespace
