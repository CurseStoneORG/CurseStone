// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityBlockChangeEvent.h — ported from cn.nukkit.event.entity.EntityBlockChangeEvent
#pragma once
#include "EntityEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace atomikpe { namespace entity { class Entity; } }
namespace atomikpe::event::entity {
class EntityBlockChangeEvent : public EntityEvent, public Cancellable {
public:
    int x = 0;
    int y = 0;
    int z = 0;
    uint8_t blockId = 0;
    uint8_t blockData = 0;

    EntityBlockChangeEvent(atomikpe::entity::Entity* e, int bx, int by, int bz, uint8_t id, uint8_t data)
        : EntityEvent(e), x(bx), y(by), z(bz), blockId(id), blockData(data) {}

    const std::string& eventName() const override {
        static std::string n = "EntityBlockChangeEvent"; return n;
    }
};
} // namespace
