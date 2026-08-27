// SPDX-License-Identifier: LGPL-3.0-or-later
// EventHandler.h — ported from cn.nukkit.event.EventHandler
//
// Annotation-style marker for event handler methods. In C++ we use it
// as a struct holding priority + ignoreCancelled, mirroring the Java
// @EventHandler annotation.
#pragma once
#include "EventPriority.h"
namespace atomikpe::event {

struct EventHandler {
    EventPriority priority = EventPriority::NORMAL;
    bool ignoreCancelled = false;

    EventHandler() = default;
    EventHandler(EventPriority p, bool ignore = false)
        : priority(p), ignoreCancelled(ignore) {}
};

} // namespace atomikpe::event
