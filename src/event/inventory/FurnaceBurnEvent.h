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
// FurnaceBurnEvent.h — ported from cn.nukkit.event.inventory.FurnaceBurnEvent
#pragma once
#include "../block/BlockEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone {
class BlockEntityFurnace;
}

namespace cursestone::event::inventory {

class FurnaceBurnEvent : public cursestone::event::block::BlockEvent, public Cancellable {
  public:
    cursestone::BlockEntityFurnace* furnace;
    cursestone::item::ItemStack fuel;
    int16_t burnTime;
    bool burning;
    FurnaceBurnEvent(cursestone::BlockEntityFurnace* f, const cursestone::item::ItemStack& fuel,
                     int16_t bt)
        : BlockEvent(0, 0, 0), furnace(f), fuel(fuel), burnTime(bt), burning(true) {
    }
    cursestone::BlockEntityFurnace* getFurnace() {
        return furnace;
    }
    cursestone::item::ItemStack getFuel() {
        return fuel;
    }
    int16_t getBurnTime() {
        return burnTime;
    }
    void setBurnTime(int16_t bt) {
        burnTime = bt;
    }
    bool isBurning() {
        return burning;
    }
    void setBurning(bool b) {
        burning = b;
    }
    const std::string& eventName() const override {
        static std::string n = "FurnaceBurnEvent";
        return n;
    }
};

} // namespace cursestone::event::inventory
