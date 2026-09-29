// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityArmorChangeEvent.h — ported from cn.nukkit.event.entity.EntityArmorChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { namespace entity { class Entity; } }
namespace cursestone::event::entity {
class EntityArmorChangeEvent : public EntityEvent, public Cancellable {
public:
    cursestone::item::ItemStack oldItem;
    cursestone::item::ItemStack newItem;
    int slot = 0;

    EntityArmorChangeEvent(cursestone::entity::Entity* e,
                           const cursestone::item::ItemStack& old,
                           const cursestone::item::ItemStack& nw,
                           int s)
        : EntityEvent(e), oldItem(old), newItem(nw), slot(s) {}

    const std::string& eventName() const override {
        static std::string n = "EntityArmorChangeEvent"; return n;
    }
};
} // namespace
