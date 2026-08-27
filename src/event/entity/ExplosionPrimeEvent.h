// SPDX-License-Identifier: LGPL-3.0-or-later
// ExplosionPrimeEvent.h — ported from cn.nukkit.event.entity.ExplosionPrimeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class ExplosionPrimeEvent : public EntityEvent, public Cancellable {
public:
    float radius = 0.0f;
    bool fireBreaking = false;

    ExplosionPrimeEvent(atomikpe::entity::Entity* e, float r, bool fb)
        : EntityEvent(e), radius(r), fireBreaking(fb) {}

    const std::string& eventName() const override {
        static std::string n = "ExplosionPrimeEvent"; return n;
    }
};
} // namespace
