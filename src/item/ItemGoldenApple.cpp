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
#include "ItemGoldenApple.h"
#include "Player.h"
#include "entity/Effect.h"
namespace cursestone::item {
// Regen II (5 s) + Absorption (2 min).
void ItemGoldenApple::applyEatEffects(Player& eater) const {
    eater.addEffect(std::make_shared<entity::Effect>(entity::EFF_REGENERATION, "Regeneration", 100, 1));
    eater.addEffect(std::make_shared<entity::Effect>(entity::EFF_ABSORPTION, "Absorption", 2400, 0));
}
} // namespace cursestone::item
