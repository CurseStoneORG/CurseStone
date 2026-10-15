/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// EventHandler.h — ported from cn.nukkit.event.EventHandler
//
// Annotation-style marker for event handler methods. In C++ we use it
// as a struct holding priority + ignoreCancelled, mirroring the Java
// @EventHandler annotation.
#pragma once
#include "EventPriority.h"
namespace cursestone::event {

struct EventHandler {
    EventPriority priority = EventPriority::NORMAL;
    bool ignoreCancelled = false;

    EventHandler() = default;
    EventHandler(EventPriority p, bool ignore = false) : priority(p), ignoreCancelled(ignore) {
    }
};

} // namespace cursestone::event
