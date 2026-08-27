// SPDX-License-Identifier: LGPL-3.0-or-later
// PotionCollideEvent.h — ported from cn.nukkit.event.potion.PotionCollideEvent
//
// Fired when a thrown splash potion collides with something (the
// ground or an entity) and is about to apply its effect. Cancellable.
#pragma once
#include "PotionEvent.h"
#include "../Cancellable.h"
#include <string>

namespace atomikpe::event::potion {

class PotionCollideEvent : public PotionEvent, public Cancellable {
public:
    void* thrownPotion;

    PotionCollideEvent(void* potion, void* tp)
        : PotionEvent(potion), thrownPotion(tp) {}

    const std::string& eventName() const override {
        static std::string n = "PotionCollideEvent"; return n;
    }
};

} // namespace atomikpe::event::potion
