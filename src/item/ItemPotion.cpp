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
#include "ItemPotion.h"
#include "Player.h"
#include "entity/Effect.h"
namespace cursestone::item {
namespace {
// Potion meta => [effect, duration in ticks, amplifier]. Plain bottles
// (0..4) carry no effect; _T suffixes are the extended variants.
struct PotionDef {
    uint8_t meta;
    uint8_t effect;
    int duration;
    int amplifier;
};
constexpr PotionDef kPotions[] = {
    {5, entity::EFF_NIGHT_VISION, 180 * 20, 0},
    {6, entity::EFF_NIGHT_VISION, 480 * 20, 0},
    {7, entity::EFF_INVISIBILITY, 180 * 20, 0},
    {8, entity::EFF_INVISIBILITY, 480 * 20, 0},
    {9, entity::EFF_JUMP, 180 * 20, 0},
    {10, entity::EFF_JUMP, 480 * 20, 0},
    {11, entity::EFF_JUMP, 90 * 20, 1},
    {12, entity::EFF_FIRE_RESISTANCE, 180 * 20, 0},
    {13, entity::EFF_FIRE_RESISTANCE, 480 * 20, 0},
    {14, entity::EFF_SPEED, 180 * 20, 0},
    {15, entity::EFF_SPEED, 480 * 20, 0},
    {16, entity::EFF_SPEED, 90 * 20, 1},
    {17, entity::EFF_SLOWNESS, 90 * 20, 0},
    {18, entity::EFF_SLOWNESS, 240 * 20, 0},
    {19, entity::EFF_WATER_BREATHING, 180 * 20, 0},
    {20, entity::EFF_WATER_BREATHING, 480 * 20, 0},
    {21, entity::EFF_INSTANT_HEALTH, 1, 0},
    {22, entity::EFF_INSTANT_HEALTH, 1, 1},
    {23, entity::EFF_INSTANT_DAMAGE, 1, 0},
    {24, entity::EFF_INSTANT_DAMAGE, 1, 1},
    {25, entity::EFF_POISON, 45 * 20, 0},
    {26, entity::EFF_POISON, 120 * 20, 0},
    {27, entity::EFF_POISON, 22 * 20, 1},
    {28, entity::EFF_REGENERATION, 45 * 20, 0},
    {29, entity::EFF_REGENERATION, 120 * 20, 0},
    {30, entity::EFF_REGENERATION, 22 * 20, 1},
    {31, entity::EFF_STRENGTH, 180 * 20, 0},
    {32, entity::EFF_STRENGTH, 480 * 20, 0},
    {33, entity::EFF_STRENGTH, 90 * 20, 1},
    {34, entity::EFF_WEAKNESS, 90 * 20, 0},
    {35, entity::EFF_WEAKNESS, 240 * 20, 0},
};
} // namespace

bool ItemPotion::effectForMeta(uint8_t meta, uint8_t& effect_id, int& duration, int& amplifier) {
    for (const auto& p : kPotions) {
        if (p.meta == meta) {
            effect_id = p.effect;
            duration = p.duration;
            amplifier = p.amplifier;
            return true;
        }
    }
    return false;
}

void ItemPotion::applyEatEffects(Player& eater, uint8_t meta) const {
    uint8_t effect_id = 0;
    int duration = 0;
    int amplifier = 0;
    if (!effectForMeta(meta, effect_id, duration, amplifier))
        return; // plain bottle: water/mundane/thick/awkward
    eater.addEffect(std::make_shared<entity::Effect>(
        entity::Effect::create(effect_id, duration, amplifier)));
}
} // namespace cursestone::item
