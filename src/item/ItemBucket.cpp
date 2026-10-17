/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| | | | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
#include "ItemBucket.h"
#include "Player.h"
namespace cursestone::item {
// Milk carries no buff: its only job is stripping every active effect
// (each one leaves through its MobEffect removal hook). Only called on
// the milk variant — the consume path gates on isFood(meta) first.
void ItemBucket::applyEatEffects(Player& eater, uint8_t meta) const {
    if (meta != META_MILK)
        return;
    eater.removeAllEffects();
}
} // namespace cursestone::item
