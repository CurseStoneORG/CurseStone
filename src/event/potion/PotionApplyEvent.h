// SPDX-License-Identifier: LGPL-3.0-or-later
// PotionApplyEvent.h — ported from cn.nukkit.event.potion.PotionApplyEvent
//
// Fired when a potion effect is about to be applied to an entity.
// Cancellable.
#pragma once
#include "PotionEvent.h"
#include "../Cancellable.h"
#include <string>

namespace atomikpe::event::potion {

class PotionApplyEvent : public PotionEvent, public Cancellable {
public:
    void* entity;

    PotionApplyEvent(void* potion, void* e)
        : PotionEvent(potion), entity(e) {}

    const std::string& eventName() const override {
        static std::string n = "PotionApplyEvent"; return n;
    }
};

} // namespace atomikpe::event::potion
