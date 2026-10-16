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
#include "Effect.h"
#include <algorithm>
namespace cursestone::entity {
namespace {
// Protocol-84 effect registry: id, name, RGB tint, bad, instant.
struct EffectDef {
    uint8_t id;
    const char* name;
    uint8_t r, g, b;
    bool bad;
    bool instant;
};
constexpr EffectDef kEffects[] = {
    {EFF_SPEED, "Speed", 124, 175, 198, false, false},
    {EFF_SLOWNESS, "Slowness", 90, 108, 129, true, false},
    {EFF_HASTE, "Haste", 217, 192, 67, false, false},
    {EFF_MINING_FATIGUE, "Mining Fatigue", 74, 66, 23, true, false},
    {EFF_STRENGTH, "Strength", 147, 36, 35, false, false},
    {EFF_INSTANT_HEALTH, "Instant Health", 248, 36, 35, false, true},
    {EFF_INSTANT_DAMAGE, "Instant Damage", 67, 10, 9, true, true},
    {EFF_JUMP, "Jump Boost", 34, 255, 76, false, false},
    {EFF_NAUSEA, "Nausea", 85, 29, 74, true, false},
    {EFF_REGENERATION, "Regeneration", 205, 92, 171, false, false},
    {EFF_RESISTANCE, "Resistance", 153, 69, 58, false, false},
    {EFF_FIRE_RESISTANCE, "Fire Resistance", 228, 154, 58, false, false},
    {EFF_WATER_BREATHING, "Water Breathing", 46, 82, 153, false, false},
    {EFF_INVISIBILITY, "Invisibility", 127, 131, 146, false, false},
    {EFF_BLINDNESS, "Blindness", 191, 192, 192, true, false},
    {EFF_NIGHT_VISION, "Night Vision", 0, 0, 139, false, false},
    {EFF_HUNGER, "Hunger", 46, 139, 87, true, false},
    {EFF_WEAKNESS, "Weakness", 72, 77, 72, true, false},
    {EFF_POISON, "Poison", 78, 147, 49, true, false},
    {EFF_WITHER, "Wither", 53, 42, 39, true, false},
    {EFF_HEALTH_BOOST, "Health Boost", 248, 125, 35, false, false},
    {EFF_ABSORPTION, "Absorption", 36, 107, 251, false, false},
    {EFF_SATURATION, "Saturation", 255, 0, 255, false, false},
};
const EffectDef* findDef(uint8_t id) {
    for (const auto& d : kEffects)
        if (d.id == id)
            return &d;
    return nullptr;
}
} // namespace
Effect Effect::create(uint8_t id, int duration_ticks, int amplifier) {
    const EffectDef* d = findDef(id);
    if (d)
        return Effect((EffectId)id, d->name, duration_ticks, amplifier);
    return Effect((EffectId)id, "Unknown", duration_ticks, amplifier);
}
uint32_t Effect::colorOf(uint8_t id) {
    const EffectDef* d = findDef(id);
    if (!d)
        return 0;
    return ((uint32_t)d->r << 16) | ((uint32_t)d->g << 8) | (uint32_t)d->b;
}
const char* Effect::nameOf(uint8_t id) {
    const EffectDef* d = findDef(id);
    return d ? d->name : "";
}
bool Effect::isBad(uint8_t id) {
    const EffectDef* d = findDef(id);
    return d && d->bad;
}
bool Effect::isInstant(uint8_t id) {
    const EffectDef* d = findDef(id);
    return d && d->instant;
}
bool Effect::canTick() const {
    int amp = std::max(0, amplifier_);
    switch (id_) {
        case EFF_POISON:
            return (25 >> amp) > 0 ? (duration_ % (25 >> amp)) == 0 : true;
        case EFF_WITHER:
            return (50 >> amp) > 0 ? (duration_ % (50 >> amp)) == 0 : true;
        case EFF_REGENERATION:
            return (40 >> amp) > 0 ? (duration_ % (40 >> amp)) == 0 : true;
        case EFF_HUNGER:
            return (duration_ % 20) == 0;
        case EFF_SATURATION:
            return (20 >> amp) > 0 ? (duration_ % (20 >> amp)) == 0 : true;
        default:
            return false;
    }
}
} // namespace cursestone::entity
