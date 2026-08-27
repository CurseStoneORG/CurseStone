// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityTeleportEvent.h — ported from cn.nukkit.event.entity.EntityTeleportEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityTeleportEvent : public EntityEvent, public Cancellable {
public:
    double fromX = 0.0;
    double fromY = 0.0;
    double fromZ = 0.0;
    double toX = 0.0;
    double toY = 0.0;
    double toZ = 0.0;

    EntityTeleportEvent(atomikpe::entity::Entity* e,
                        double fx, double fy, double fz,
                        double tx, double ty, double tz)
        : EntityEvent(e), fromX(fx), fromY(fy), fromZ(fz),
          toX(tx), toY(ty), toZ(tz) {}

    const std::string& eventName() const override {
        static std::string n = "EntityTeleportEvent"; return n;
    }
};
} // namespace
