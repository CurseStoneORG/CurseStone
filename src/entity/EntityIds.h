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
// EntityIds — network entity type IDs for protocol 84. Used in
// AddEntityPacket to tell the client which model/texture to load.
#pragma once
#include <cstdint>
namespace cursestone::entity {
enum EntityId : int32_t {
    EITEM_ITEM = 1,
    EITEM_XP_ORB = 2,
    EITEM_ENDER_PEARL = 14,
    // Protocol 84 FallingSand (Infinity FallingSand.php NETWORK_ID = 66).
    // This used to say 10, which is the CHICKEN type on protocol 84 (a
    // latent bug: the constant was never sent — the falling-block spawn
    // path now uses AddEntityPacket::TYPE_FALLING_BLOCK, same value).
    EITEM_FALLING_BLOCK = 66,
    EITEM_PAINTING = 9,
    EITEM_PRIMED_TNT = 65,
    EITEM_ARROW = 80,
    EITEM_SNOWBALL = 81,
    EITEM_EGG = 82,
    EITEM_FIREBALL = 83,
    EITEM_SMALL_FIREBALL = 94,
    EITEM_ENDER_CRYSTAL = 71,
    EITEM_PLAYER = 63,

    // passive mobs
    EITEM_CHICKEN = 10,
    EITEM_COW = 11,
    EITEM_PIG = 12,
    EITEM_SHEEP = 13,
    EITEM_WOLF = 14,
    EITEM_VILLAGER = 15,
    EITEM_HORSE = 23,
    EITEM_RABBIT = 18,

    // hostile mobs
    EITEM_ZOMBIE = 32,
    EITEM_CREEPER = 33,
    EITEM_SKELETON = 34,
    EITEM_SPIDER = 35,
    EITEM_ENDERMAN = 38,
    EITEM_BLAZE = 43,
    EITEM_SLIME = 37,
    EITEM_GHAST = 41,

    // environmental
    EITEM_BOAT = 90,
    EITEM_MINECART = 97,
};
} // namespace
