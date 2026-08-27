// SPDX-License-Identifier: LGPL-3.0-or-later
// ProjectileLaunchEvent.h — ported from cn.nukkit.event.entity.ProjectileLaunchEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class ProjectileLaunchEvent : public EntityEvent, public Cancellable {
public:
    explicit ProjectileLaunchEvent(atomikpe::entity::Entity* e) : EntityEvent(e) {}

    const std::string& eventName() const override {
        static std::string n = "ProjectileLaunchEvent"; return n;
    }
};
} // namespace
