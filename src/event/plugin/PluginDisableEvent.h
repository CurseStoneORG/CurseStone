// SPDX-License-Identifier: LGPL-3.0-or-later
// PluginDisableEvent.h — ported from cn.nukkit.event.plugin.PluginDisableEvent
//
// Fired when a plugin is disabled.
#pragma once
#include "PluginEvent.h"
#include <string>

namespace cursestone::event::plugin {

class PluginDisableEvent : public PluginEvent {
public:
    explicit PluginDisableEvent(void* p) : PluginEvent(p) {}

    const std::string& eventName() const override {
        static std::string n = "PluginDisableEvent"; return n;
    }
};

} // namespace cursestone::event::plugin
