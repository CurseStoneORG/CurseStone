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
// PotionEvent.h — ported from cn.nukkit.event.potion.PotionEvent
//
// Base class for all potion-related events. Holds an opaque pointer
// to the potion (Effect) instance.
#pragma once
#include "../Event.h"
#include <string>

namespace cursestone::event::potion {

class PotionEvent : public Event {
public:
    void* potion;

    explicit PotionEvent(void* p) : potion(p) {}

    const std::string& eventName() const override {
        static std::string n = "PotionEvent"; return n;
    }
};

} // namespace cursestone::event::potion
