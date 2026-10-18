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
#pragma once
#include "Item.h"
#include <string>
namespace cursestone::item {
class ItemBow : public Item {
  public:
    ItemBow() : Item(261) {
    }
    std::string name() const override {
        return "Bow";
    }
    // The bow carries durability in its damage value, so it can never
    // stack (the reference server's Bow extends Tool, whose max stack
    // is 1). Without this a pickup of two bows merged them into one
    // illegal count=2 slot — a state the 0.15.10 client treats as
    // invalid: the stack stopped rendering in other players' hands and
    // the client aborted the draw/release handshake, so no arrow ever
    // spawned.
    int maxStackSize() const override {
        return 1;
    }
};
} // namespace
