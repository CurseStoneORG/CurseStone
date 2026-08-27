// SPDX-License-Identifier: LGPL-3.0-or-later
// PotionEvent.h — ported from cn.nukkit.event.potion.PotionEvent
//
// Base class for all potion-related events. Holds an opaque pointer
// to the potion (Effect) instance.
#pragma once
#include "../Event.h"
#include <string>

namespace atomikpe::event::potion {

class PotionEvent : public Event {
public:
    void* potion;

    explicit PotionEvent(void* p) : potion(p) {}

    const std::string& eventName() const override {
        static std::string n = "PotionEvent"; return n;
    }
};

} // namespace atomikpe::event::potion
