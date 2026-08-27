// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityInventoryChangeEvent.h — ported from cn.nukkit.event.entity.EntityInventoryChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityInventoryChangeEvent : public EntityEvent, public Cancellable {
public:
    atomikpe::item::ItemStack oldItem;
    atomikpe::item::ItemStack newItem;
    int slot = 0;

    EntityInventoryChangeEvent(atomikpe::entity::Entity* e,
                               const atomikpe::item::ItemStack& old,
                               const atomikpe::item::ItemStack& nw,
                               int s)
        : EntityEvent(e), oldItem(old), newItem(nw), slot(s) {}

    const std::string& eventName() const override {
        static std::string n = "EntityInventoryChangeEvent"; return n;
    }
};
} // namespace
