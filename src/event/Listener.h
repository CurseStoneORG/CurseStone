// SPDX-License-Identifier: LGPL-3.0-or-later
// Listener.h — ported from cn.nukkit.event.Listener
//
// Marker interface for event listener classes. In C++ this is an empty
// base class that listener classes inherit from.
#pragma once
namespace atomikpe::event {

class Listener {
public:
    virtual ~Listener() = default;
};

} // namespace atomikpe::event
