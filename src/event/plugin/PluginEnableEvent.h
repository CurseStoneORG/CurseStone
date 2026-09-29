// SPDX-License-Identifier: LGPL-3.0-or-later
// PluginEnableEvent.h — ported from cn.nukkit.event.plugin.PluginEnableEvent
//
// Fired when a plugin is enabled.
#pragma once
#include "PluginEvent.h"
#include <string>

namespace cursestone::event::plugin {

class PluginEnableEvent : public PluginEvent {
public:
    explicit PluginEnableEvent(void* p) : PluginEvent(p) {}

    const std::string& eventName() const override {
        static std::string n = "PluginEnableEvent"; return n;
    }
};

} // namespace cursestone::event::plugin
