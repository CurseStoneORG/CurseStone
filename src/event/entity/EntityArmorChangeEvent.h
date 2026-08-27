// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityArmorChangeEvent.h — ported from cn.nukkit.event.entity.EntityArmorChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityArmorChangeEvent : public EntityEvent, public Cancellable {
public:
    atomikpe::item::ItemStack oldItem;
    atomikpe::item::ItemStack newItem;
    int slot = 0;

    EntityArmorChangeEvent(atomikpe::entity::Entity* e,
                           const atomikpe::item::ItemStack& old,
                           const atomikpe::item::ItemStack& nw,
                           int s)
        : EntityEvent(e), oldItem(old), newItem(nw), slot(s) {}

    const std::string& eventName() const override {
        static std::string n = "EntityArmorChangeEvent"; return n;
    }
};
} // namespace
