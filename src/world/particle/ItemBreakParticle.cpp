/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// ItemBreakParticle.cpp — payload validation against the item registry.
#include "ItemBreakParticle.h"
#include "item/Item.h"

namespace cursestone::world::particle {

bool ItemBreakParticle::validData(int32_t data) {
    // The client renders the item stored in bits 16..31; the low half is
    // the damage. An id the client has no registered item for (0 included)
    // NPEs its renderer and closes the game — reject instead of sending.
    const uint32_t raw = (uint32_t)data;
    const uint16_t item_id = (uint16_t)((raw >> 16) & 0xFFFFu);
    if (item_id == 0)
        return false;
    return item::Item::get(item_id) != nullptr;
}

} // namespace cursestone::world::particle
