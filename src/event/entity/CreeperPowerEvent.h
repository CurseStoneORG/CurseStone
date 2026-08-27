// SPDX-License-Identifier: LGPL-3.0-or-later
// CreeperPowerEvent.h — ported from cn.nukkit.event.entity.CreeperPowerEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class CreeperPowerEvent : public EntityEvent, public Cancellable {
public:
    static constexpr int POWER_CAUSE_LIGHTNING = 0;
    static constexpr int POWER_CAUSE_SET_ON_FIRE = 1;
    static constexpr int POWER_CAUSE_TNT = 2;

    int powerCause = 0;

    CreeperPowerEvent(atomikpe::entity::Entity* e, int cause)
        : EntityEvent(e), powerCause(cause) {}

    const std::string& eventName() const override {
        static std::string n = "CreeperPowerEvent"; return n;
    }
};
} // namespace
