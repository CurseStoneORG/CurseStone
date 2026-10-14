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

namespace cursestone::item {

using enchantment::EnchantmentId;
using enchantment::EnchantmentList;

void enchantmentsFromNbt(const mcp_tag_t* entry, EnchantmentList& out) {
    const mcp_tag_t* list = mcp_compound_tag_get(entry, kNbtEnch);
    if (!list || mcp_tag_get_type(list) != MCP_TAG_LIST) return;

    const size_t n = mcp_list_tag_size(list);
    for (size_t i = 0; i < n; ++i) {
        const mcp_tag_t* c = mcp_list_tag_get(list, i);
        if (!c || mcp_tag_get_type(c) != MCP_TAG_COMPOUND) continue;

        const int id = mcp_compound_tag_get_short(c, kNbtId, -1);
        // "lvl" is what Bedrock/Genisys write; accept the spelled-out
        // "level" for Nukkit-era data. A missing/zero level is not an
        // enchantment at all (it would never change a lookup), so it is
        // dropped rather than stored as a no-op entry.
        int lvl = mcp_compound_tag_get_short(c, kNbtEnchLvl, 0);
        if (lvl == 0 && mcp_compound_tag_contains(c, kNbtEnchLvlAlt)) {
            lvl = mcp_compound_tag_get_short(c, kNbtEnchLvlAlt, 0);
        }
        if (id < 0 || lvl <= 0) continue;
        if (lvl > 255) lvl = 255;   // levels are a byte on the wire

        // Enchantment ids the pipeline does not implement are kept as-is:
        // EnchantmentId is a uint16_t-backed enum, and a stored id is only
        // ever matched by a query for that same id, so an unknown one is
        // inert rather than corrupting anything. Dropping it here would
        // instead silently lose an enchantment on a save/load round trip.
        out.add(static_cast<EnchantmentId>(static_cast<uint16_t>(id)),
                static_cast<uint8_t>(lvl));
    }
}

void itemStackToNbt(const ItemStack& stack, mcp_tag_t* entry) {
    if (!entry || mcp_tag_get_type(entry) != MCP_TAG_COMPOUND) return;

    mcp_compound_tag_set_short(entry, kNbtId,     static_cast<int16_t>(stack.id));
    mcp_compound_tag_set_byte (entry, kNbtCount,  static_cast<int8_t>(stack.count));
    mcp_compound_tag_set_short(entry, kNbtDamage, static_cast<int16_t>(stack.meta));

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
        mcp_tag_t* tag = mcp_nbt_read_bytes(
            reinterpret_cast<const uint8_t*>(stack.nbt.data()),
            stack.nbt.size(), /*little_endian=*/true);
        if (tag && mcp_tag_get_type(tag) == MCP_TAG_COMPOUND) {
            // Ownership transfers to `entry` (mcp_compound_tag_set stores
            // the pointer and destroys it together with the parent).
            mcp_compound_tag_set(entry, kNbtTag, tag);
        } else {
            // Malformed blob — drop it rather than persist garbage.
            mcp_tag_destroy(tag);
        }
    } else {
        // An unnamed item must not drag a stale "tag" from a previous
        // incarnation of this entry along.
        mcp_compound_tag_remove(entry, kNbtTag);
    }

    const auto& entries = stack.ench.entries();
    if (entries.empty()) {
        // Keep a plain item's layout identical to the pre-enchantment one:
        // no empty "ench" list, and no stale one either if this entry is
        // being rewritten.
        mcp_compound_tag_remove(entry, kNbtEnch);
        return;
    }

    mcp_tag_t* list = mcp_list_tag_new_named(kNbtEnch, MCP_TAG_COMPOUND);
    if (!list) return;
    for (const auto& e : entries) {
        mcp_tag_t* c = mcp_compound_tag_new();
        if (!c) continue;
        mcp_compound_tag_set_short(c, kNbtId,       static_cast<int16_t>(e.id));
        mcp_compound_tag_set_short(c, kNbtEnchLvl,  static_cast<int16_t>(e.level));
        if (mcp_list_tag_append(list, c) != 0) mcp_tag_destroy(c);
    }
    mcp_compound_tag_set(entry, kNbtEnch, list);
}

bool itemStackFromNbt(const mcp_tag_t* entry, ItemStack& out) {
    out = ItemStack::EMPTY();
    if (!entry || mcp_tag_get_type(entry) != MCP_TAG_COMPOUND) return false;

    const uint16_t id    = static_cast<uint16_t>(mcp_compound_tag_get_short(entry, kNbtId, 0));
    const int      count = mcp_compound_tag_get_byte(entry, kNbtCount, 0);
    const uint8_t  meta  = static_cast<uint8_t>(mcp_compound_tag_get_short(entry, kNbtDamage, 0));

    if (id == 0 || count <= 0) return false;

    out.id    = id;
    out.count = count;
    out.meta  = meta;
    enchantmentsFromNbt(entry, out.ench);

    // ---- Restore the vanilla "tag" compound as the raw network blob ----
    // Re-serializing the parsed tag tree little-endian rebuilds the wire
    // format ItemStack::nbt carries ({display:{Name}} and anything else a
    // plugin or the client stored), so custom names survive a relog
    // instead of coming back nameless.
    //
    // Header fix-up: after a save/load cycle the parsed "tag" child
    // carries its own key name ("tag"), and mcp_nbt_write_bytes() writes
    // the root's name field. The network format always has an EMPTY root
    // name, so strip the emitted name header (type byte + u16 LE length
    // + name) and re-emit just type + zero length + payload. Avoids
    // mutating the caller's tree through a const pointer.
    const mcp_tag_t* tag = mcp_compound_tag_get(entry, kNbtTag);
    if (tag && mcp_tag_get_type(tag) == MCP_TAG_COMPOUND) {
        size_t raw_len = 0;
        uint8_t* raw = mcp_nbt_write_bytes(tag, /*little_endian=*/true, &raw_len);
        if (raw && raw_len >= 3) {
            const uint16_t name_len =
                (uint16_t)raw[1] | ((uint16_t)raw[2] << 8);
            const size_t payload_off = 3u + name_len;   // type + len + name
            if (payload_off <= raw_len) {
                out.nbt.reserve(3 + (raw_len - payload_off));
                out.nbt.push_back((char)0x0A);           // TAG_Compound root
                out.nbt.push_back('\0');                 // empty root name
                out.nbt.push_back('\0');
                out.nbt.append(reinterpret_cast<const char*>(raw) + payload_off,
                               raw_len - payload_off);
            }
        }
        free(raw);
        // Vanilla-authored data keeps "ench" INSIDE the tag compound; if
        // the entry-level list (CurseStone's own layout) was absent, read
        // it from there too so external player files keep their enchants.
        if (out.ench.entries().empty()) enchantmentsFromNbt(tag, out.ench);
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
    if (name.empty()) return {};

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
    blob.push_back((char)0x0A);           // TAG_Compound (root)
    blob += u16le(0);                     // root name ""
    blob.push_back((char)0x0A);           // TAG_Compound "display"
    blob += tagString("display");
    blob.push_back((char)0x08);           // TAG_String "Name"
    blob += tagString("Name");
    blob += tagString(name);              // the custom name itself
    blob.push_back((char)0x00);           // end of "display"
    blob.push_back((char)0x00);           // end of root
    return blob;
}

} // namespace cursestone::item
