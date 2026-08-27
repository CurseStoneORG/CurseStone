// SPDX-License-Identifier: LGPL-3.0-or-later
// ItemDespawnEvent.h — ported from cn.nukkit.event.entity.ItemDespawnEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class ItemDespawnEvent : public EntityEvent, public Cancellable {
public:
    explicit ItemDespawnEvent(atomikpe::entity::Entity* e) : EntityEvent(e) {}

    const std::string& eventName() const override {
        static std::string n = "ItemDespawnEvent"; return n;
    }
};
} // namespace
