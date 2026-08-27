// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityExplodeEvent.h — ported from cn.nukkit.event.entity.EntityExplodeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include <vector>
#include <tuple>
#include <string>
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityExplodeEvent : public EntityEvent, public Cancellable {
public:
    int x = 0;
    int y = 0;
    int z = 0;
    std::vector<std::tuple<int,int,int>> blocks;
    float yield = 0.0f;

    EntityExplodeEvent(atomikpe::entity::Entity* e, int bx, int by, int bz,
                        std::vector<std::tuple<int,int,int>> bl, float yld)
        : EntityEvent(e), x(bx), y(by), z(bz), blocks(std::move(bl)), yield(yld) {}

    const std::string& eventName() const override {
        static std::string n = "EntityExplodeEvent"; return n;
    }
};
} // namespace
