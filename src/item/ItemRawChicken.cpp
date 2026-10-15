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
#include "ItemRawChicken.h"
#include "Player.h"
#include "entity/Effect.h"
#include <cstdlib>
namespace cursestone::item {
// 30% chance of Hunger for 30 s after eating.
void ItemRawChicken::applyEatEffects(Player& eater) const {
    if (std::rand() % 100 < 30)
        eater.addEffect(std::make_shared<entity::Effect>(entity::EFF_HUNGER, "Hunger", 600, 0));
}
} // namespace cursestone::item
