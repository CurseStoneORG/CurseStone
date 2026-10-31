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
#include "item/ItemNbt.h"

#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
#include "nbt/NBT.h"
#include "utils/CRaii.h"

namespace cursestone::item {

using enchantment::EnchantmentId;
using enchantment::EnchantmentList;

void enchantmentsFromNbt(const cursestone::nbt::Tag* entry, EnchantmentList& out) {
    const cursestone::nbt::Tag* list = cursestone::nbt::get(entry, kNbtEnch);
    if (!list || cursestone::nbt::type_of(list) != cursestone::nbt::TAG_LIST)
        return;

    const size_t n = cursestone::nbt::size(list);
    for (size_t i = 0; i < n; ++i) {
        const cursestone::nbt::Tag* c = cursestone::nbt::at(list, i);
        if (!c || cursestone::nbt::type_of(c) != cursestone::nbt::TAG_COMPOUND)
            continue;

        const int id = cursestone::nbt::get_short(c, kNbtId, -1);
        // "lvl" is what Bedrock/Genisys write; accept the spelled-out
        // "level" for Nukkit-era data. A missing/zero level is not an
        // enchantment at all (it would never change a lookup), so it is
        // dropped rather than stored as a no-op entry.
        int lvl = cursestone::nbt::get_short(c, kNbtEnchLvl, 0);
        if (lvl == 0 && cursestone::nbt::contains(c, kNbtEnchLvlAlt)) {
            lvl = cursestone::nbt::get_short(c, kNbtEnchLvlAlt, 0);
        }
        if (id < 0 || lvl <= 0)
            continue;
        if (lvl > 255)
            lvl = 255; // levels are a byte on the wire

        // Enchantment ids the pipeline does not implement are kept as-is:
        // EnchantmentId is a uint16_t-backed enum, and a stored id is only
        // ever matched by a query for that same id, so an unknown one is
        // inert rather than corrupting anything. Dropping it here would
        // instead silently lose an enchantment on a save/load round trip.
        out.add(static_cast<EnchantmentId>(static_cast<uint16_t>(id)), static_cast<uint8_t>(lvl));
    }
}

void itemStackToNbt(const ItemStack& stack, cursestone::nbt::Tag* entry) {
    if (!entry || cursestone::nbt::type_of(entry) != cursestone::nbt::TAG_COMPOUND)
        return;

    cursestone::nbt::set_short(entry, kNbtId, static_cast<int16_t>(stack.id));
    cursestone::nbt::set_byte(entry, kNbtCount, static_cast<int8_t>(stack.count));
    cursestone::nbt::set_short(entry, kNbtDamage, static_cast<int16_t>(stack.meta));

    // ---- Persist the stack's raw network NBT under the vanilla "tag"
    // compound ----
    // ItemStack::nbt holds the little-endian network compound the 0.15
    // client puts on the wire ({display:{Name}}, plugin custom data, ...).
    // Parsing it into a real sub-compound (instead of, say, an opaque
    // string tag) keeps the on-disk layout vanilla-compatible AND
    // endian-proof: the tag tree stores parsed values, so re-serializing
    // it little-endian on load rebuilds the exact wire format regardless
    // of how the player.dat itself is encoded.
    if (!stack.nbt.empty()) {
        utils::TagGuard tag(cursestone::nbt::read(reinterpret_cast<const uint8_t*>(stack.nbt.data()), stack.nbt.size(), /*little_endian=*/true));
        if (tag && cursestone::nbt::type_of(tag.get()) == cursestone::nbt::TAG_COMPOUND &&
            !cursestone::nbt::set(entry, kNbtTag, tag.get())) {
            // Ownership transferred to `entry` (nbt::set stores
            // the pointer and destroys it together with the parent).
            tag.release();
        }
        // else: malformed blob — the guard drops it rather than persist garbage.
    } else {
        // An unnamed item must not drag a stale "tag" from a previous
        // incarnation of this entry along.
        cursestone::nbt::remove(entry, kNbtTag);
    }

    const auto& entries = stack.ench.entries();
    if (entries.empty()) {
        // Keep a plain item's layout identical to the pre-enchantment one:
        // no empty "ench" list, and no stale one either if this entry is
        // being rewritten.
        cursestone::nbt::remove(entry, kNbtEnch);
        return;
    }

    utils::TagGuard list(new cursestone::nbt::ListTag(kNbtEnch, cursestone::nbt::TAG_COMPOUND));
    if (!list)
        return;
    for (const auto& e : entries) {
        utils::TagGuard c(new cursestone::nbt::CompoundTag());
        if (!c)
            continue;
        cursestone::nbt::set_short(c.get(), kNbtId, static_cast<int16_t>(e.id));
        cursestone::nbt::set_short(c.get(), kNbtEnchLvl, static_cast<int16_t>(e.level));
        if (!cursestone::nbt::append(list.get(), c.get()))
            c.release(); // list owns it now; the guard only covers append failure.
    }
    if (!cursestone::nbt::set(entry, kNbtEnch, list.get()))
        list.release();
}

bool itemStackFromNbt(const cursestone::nbt::Tag* entry, ItemStack& out) {
    out = ItemStack::EMPTY();
    if (!entry || cursestone::nbt::type_of(entry) != cursestone::nbt::TAG_COMPOUND)
        return false;

    const uint16_t id = static_cast<uint16_t>(cursestone::nbt::get_short(entry, kNbtId, 0));
    const int count = cursestone::nbt::get_byte(entry, kNbtCount, 0);
    const uint8_t meta = static_cast<uint8_t>(cursestone::nbt::get_short(entry, kNbtDamage, 0));

    if (id == 0 || count <= 0)
        return false;

    out.id = id;
    out.count = count;
    out.meta = meta;
    enchantmentsFromNbt(entry, out.ench);

    // ---- Restore the vanilla "tag" compound as the raw network blob ----
    // Re-serializing the parsed tag tree little-endian rebuilds the wire
    // format ItemStack::nbt carries ({display:{Name}} and anything else a
    // plugin or the client stored), so custom names survive a relog
    // instead of coming back nameless.
    //
    // Header fix-up: after a save/load cycle the parsed "tag" child
    // carries its own key name ("tag"), and nbt::write() emits
    // the root's name field. The network format always has an EMPTY root
    // name, so strip the emitted name header (type byte + u16 LE length
    // + name) and re-emit just type + zero length + payload. Avoids
    // mutating the caller's tree through a const pointer.
    const cursestone::nbt::Tag* tag = cursestone::nbt::get(entry, kNbtTag);
    if (tag && cursestone::nbt::type_of(tag) == cursestone::nbt::TAG_COMPOUND) {
        std::vector<uint8_t> raw;
        cursestone::nbt::write(tag, /*little_endian=*/true, raw);
        if (raw.size() >= 3) {
            const uint16_t name_len = (uint16_t)raw[1] | ((uint16_t)raw[2] << 8);
            const size_t payload_off = 3u + name_len; // type + len + name
            if (payload_off <= raw.size()) {
                out.nbt.reserve(3 + (raw.size() - payload_off));
                out.nbt.push_back((char)0x0A); // TAG_Compound root
                out.nbt.push_back('\0');       // empty root name
                out.nbt.push_back('\0');
                out.nbt.append(reinterpret_cast<const char*>(raw.data()) + payload_off,
                               raw.size() - payload_off);
            }
        }
        // Vanilla-authored data keeps "ench" INSIDE the tag compound; if
        // the entry-level list (CurseStone's own layout) was absent, read
        // it from there too so external player files keep their enchants.
        if (out.ench.entries().empty())
            enchantmentsFromNbt(tag, out.ench);
    }
    return true;
}

// ============================================================
// buildDisplayNbt — the little-endian network NBT compound the
// 0.15.10 client reads a custom item name from, byte-exact with what
// Genisys Item::setCustomName() ends up putting on the wire:
//
//     0x0A <u16 len=0>                     root compound, empty name
//       0x0A <u16 len> "display"           display compound
//         0x08 <u16 len> "Name" <utf8>     the visible name
//       0x00                               end display
//     0x00                                 end root
//
// TAG_String is <u16 LE length> + bytes (old PE NBT). An empty name
// yields an empty blob (no NBT at all), exactly like an unnamed item.
// The slot writer prefixes this with the u16 LE blob length.
// ============================================================
std::string buildDisplayNbt(const std::string& name) {
    if (name.empty())
        return {};

    auto u16le = [](uint16_t v) {
        std::string s;
        s.push_back((char)(v & 0xFF));
        s.push_back((char)((v >> 8) & 0xFF));
        return s;
    };
    auto tagString = [&u16le](const std::string& payload) {
        return u16le((uint16_t)payload.size()) + payload;
    };

    std::string blob;
    blob.push_back((char)0x0A); // TAG_Compound (root)
    blob += u16le(0);           // root name ""
    blob.push_back((char)0x0A); // TAG_Compound "display"
    blob += tagString("display");
    blob.push_back((char)0x08); // TAG_String "Name"
    blob += tagString("Name");
    blob += tagString(name);    // the custom name itself
    blob.push_back((char)0x00); // end of "display"
    blob.push_back((char)0x00); // end of root
    return blob;
}

} // namespace cursestone::item
