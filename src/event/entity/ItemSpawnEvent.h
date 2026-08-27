// SPDX-License-Identifier: LGPL-3.0-or-later
// ItemSpawnEvent.h — ported from cn.nukkit.event.entity.ItemSpawnEvent
#pragma once
#include "EntityEvent.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class ItemSpawnEvent : public EntityEvent {
public:
    explicit ItemSpawnEvent(atomikpe::entity::Entity* e) : EntityEvent(e) {}

    const std::string& eventName() const override {
        static std::string n = "ItemSpawnEvent"; return n;
    }
};
} // namespace
