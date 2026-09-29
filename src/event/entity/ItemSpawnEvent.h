// SPDX-License-Identifier: LGPL-3.0-or-later
// ItemSpawnEvent.h — ported from cn.nukkit.event.entity.ItemSpawnEvent
#pragma once
#include "EntityEvent.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class ItemSpawnEvent : public EntityEvent {
public:
    explicit ItemSpawnEvent(cursestone::entity::Entity* e) : EntityEvent(e) {}

    const std::string& eventName() const override {
        static std::string n = "ItemSpawnEvent"; return n;
    }
};
} // namespace
