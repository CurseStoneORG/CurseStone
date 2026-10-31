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
// ============================================================
// ItemNbt — an item <-> NBT in ONE place.
//
// The entry layout is the client's own: net.minecraft.world.item.
// ItemInstance::save/load (handheld/src/world/item/ItemInstance.cpp),
// which is what every container entry in the world uses:
//
//     id      SHORT    item id
//     Count   BYTE     stack size
//     Damage  SHORT    aux value (durability for tools/armor)
//
// on top of it, the enchantment list, which the client's 2013 loader
// never had but Bedrock/Genisys always stored:
//
//     ench    LIST<COMPOUND>  { id SHORT, lvl SHORT }
//
// That second tag is the whole point of this module: the damage pipeline
// already queries the armour's EPF and the weapon's Sharpness / Knockback /
// Fire Aspect, but nothing ever filled ItemStack::ench from disk, so every
// one of those lookups read 0. Items now load their enchantments on the
// same path that loads their id.
//
// The same three fields used to be hand-copied in four loaders/savers
// (Player::saveData, TileChest, TileFurnace, Player::loadNBT) — which is
// also why enchantments could never have worked in any of them. They all
// go through here now.
// ============================================================
#pragma once

#include "nbt/tag/Tag.h"
#include "item/ItemStack.h"

namespace cursestone::item {

// NBT keys of an item entry (see the header comment).
constexpr const char* kNbtId = "id";
constexpr const char* kNbtCount = "Count";
constexpr const char* kNbtDamage = "Damage";
constexpr const char* kNbtEnch = "ench";
// Bedrock/Genisys write the enchantment level as "lvl"; a few Nukkit-era
// writers spell it out as "level". The reader accepts both, the writer
// always emits "lvl".
constexpr const char* kNbtEnchLvl = "lvl";
constexpr const char* kNbtEnchLvlAlt = "level";
// Vanilla item-extra-data key. Java saves the client's {display:{Name}},
// {ench}, etc. compound under "tag"; CurseStone persists the stack's raw
// little-endian network NBT blob (ItemStack::nbt) the same way so a
// custom-named item survives player/chest/furnace save + load round
// trips. See itemStackToNbt()/itemStackFromNbt().
constexpr const char* kNbtTag = "tag";

// Serialize `stack` into the compound `entry`, replacing any previous
// id/Count/Damage/ench it held. A stack with no enchantments has its
// "ench" key removed rather than written as an empty list, so ordinary
// items keep the exact byte layout they had before this module existed.
// No-op if `entry` is not a CompoundTag.
void itemStackToNbt(const ItemStack& stack, cursestone::nbt::Tag* entry);

// Read a container entry back. Returns false when `entry` is not a
// compound or holds no item (id == 0 or Count <= 0) — in that case `out` is
// left empty. Otherwise id / meta / count / enchantments all come back.
bool itemStackFromNbt(const cursestone::nbt::Tag* entry, ItemStack& out);

// Just the "ench" list of `entry`, decoded into `out` (which is NOT
// cleared first: entries accumulate, which is what EnchantmentList::add
// is for). Exposed for callers that already have the item fields, and for
// the regression test.
void enchantmentsFromNbt(const cursestone::nbt::Tag* entry, enchantment::EnchantmentList& out);

// Builds the little-endian network NBT compound that gives an item a
// custom visible name on 0.15.10 clients: {display:{Name:"..."}}. The
// returned blob goes verbatim into ItemStack::nbt; every slot writer
// prefixes it with its u16 LE length (Genisys putSlot). An empty name
// yields an empty blob (an unnamed item has no NBT at all).
std::string buildDisplayNbt(const std::string& name);

} // namespace cursestone::item
