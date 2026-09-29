// SPDX-License-Identifier: LGPL-3.0-or-later
// ProjectileHitEvent.h — ported from cn.nukkit.event.entity.ProjectileHitEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class ProjectileHitEvent : public EntityEvent, public Cancellable {
public:
    int x = 0;
    int y = 0;
    int z = 0;

    ProjectileHitEvent(cursestone::entity::Entity* e, int bx, int by, int bz)
        : EntityEvent(e), x(bx), y(by), z(bz) {}

    const std::string& eventName() const override {
        static std::string n = "ProjectileHitEvent"; return n;
    }
};
} // namespace
