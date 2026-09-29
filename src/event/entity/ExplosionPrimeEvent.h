// SPDX-License-Identifier: LGPL-3.0-or-later
// ExplosionPrimeEvent.h — ported from cn.nukkit.event.entity.ExplosionPrimeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class ExplosionPrimeEvent : public EntityEvent, public Cancellable {
public:
    float radius = 0.0f;
    bool fireBreaking = false;

    ExplosionPrimeEvent(cursestone::entity::Entity* e, float r, bool fb)
        : EntityEvent(e), radius(r), fireBreaking(fb) {}

    const std::string& eventName() const override {
        static std::string n = "ExplosionPrimeEvent"; return n;
    }
};
} // namespace
