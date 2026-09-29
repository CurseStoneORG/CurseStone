// SPDX-License-Identifier: LGPL-3.0-or-later
// PluginEvent.h — ported from cn.nukkit.event.plugin.PluginEvent
//
// Base class for all plugin-related events (enable / disable). Holds
// an opaque pointer to the plugin instance.
#pragma once
#include "../Event.h"
#include <string>

namespace cursestone::event::plugin {

class PluginEvent : public Event {
public:
    void* plugin;

    explicit PluginEvent(void* p) : plugin(p) {}

    const std::string& eventName() const override {
        static std::string n = "PluginEvent"; return n;
    }
};

} // namespace cursestone::event::plugin
