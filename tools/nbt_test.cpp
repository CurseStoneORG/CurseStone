// tools/nbt_test.cpp
//
// Regression test for the NBT binary format (src/nbt/, the port of
// handheld/src/nbt/) and for item NBT (src/item/ItemNbt.h, the port of
// ItemInstance::save/load plus the "ench" list the 2013 client never had).
//
// The format half works from HAND-WRITTEN LEVEL-BYTE FIXTURES, so it checks
// the layout against the spec instead of against our own writer:
//   * parse a little-endian fixture containing all 11 tag types and read
//     every field back,
//   * re-serialize it and require the bytes to come out IDENTICAL,
//   * same for a big-endian fixture (lengths and payloads),
//   * truncated / bogus input must fail cleanly instead of reading past the
//     buffer.
//
// The item half then takes the hand-written item entry (a used diamond
// sword with Sharpness III and Knockback II) and follows it NBT bytes ->
// ItemStack, where the enchantment levels finally stop being zero.
//
//   tools/run_nbt_test.sh
#include "nbt/NBT.h"
#include "nbt/tag/ByteArrayTag.h"
#include "nbt/tag/ByteTag.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/IntArrayTag.h"
#include "nbt/tag/IntTag.h"
#include "nbt/tag/ListTag.h"
#include "item/ItemNbt.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using cursestone::item::ItemStack;
using cursestone::item::enchantment::EnchantmentId;

static int failures = 0;

static void check(bool ok, const std::string& name) {
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", name.c_str());
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-4) {
    return std::fabs(a - b) <= eps;
}

// RAII-free helper: parse a fixture, hand the tag to `fn`, always destroy it.
template <typename Fn>
static void withRoot(const uint8_t* data, size_t len, bool le, Fn fn) {
    cursestone::nbt::Tag* root = cursestone::nbt::read(data, len, le);
    fn(root);
    if (root) delete root;
}

// Serialize any tag and compare with a byte fixture.
static std::vector<uint8_t> serialize(const cursestone::nbt::Tag* tag, bool le) {
    std::vector<uint8_t> out;
    cursestone::nbt::write(tag, le, out);
    return out;
}
static bool bytesEqual(const std::vector<uint8_t>& a, const uint8_t* b, size_t n) {
    return a.size() == n && std::memcmp(a.data(), b, n) == 0;
}

// ============================================================
// Little-endian fixture: every tag type the format defines.
// Format per entry: type byte, name length (u16 LE), name, payload.
// ============================================================
static const uint8_t kLeRoot[] = {
    0x0A,                               // TAG_Compound (root)
    0x04, 0x00, 'R','o','o','t',        // root name = "Root"
    0x01, 0x01, 0x00, 'B', 0x7F,        // BYTE   "B"  = 127
    0x02, 0x01, 0x00, 'S', 0x39, 0x30,  // SHORT  "S"  = 12345
    0x03, 0x01, 0x00, 'I',              // INT    "I"  = 0x12345678
          0x78, 0x56, 0x34, 0x12,
    0x04, 0x01, 0x00, 'L',              // LONG   "L"  = 0x0102030405060708
          0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x05, 0x01, 0x00, 'F',              // FLOAT  "F"  = 1.0
          0x00, 0x00, 0x80, 0x3F,
    0x06, 0x01, 0x00, 'D',              // DOUBLE "D"  = 1.5
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F,
    0x07, 0x02, 0x00, 'B','A',          // BYTE_ARRAY "BA" = {0xAA,0xBB}
          0x02, 0x00, 0x00, 0x00, 0xAA, 0xBB,
    0x08, 0x02, 0x00, 'S','t',          // STRING "St" = "hi"
          0x02, 0x00, 'h','i',
    0x09, 0x02, 0x00, 'L','i',          // LIST "Li" of INT, 2 elements
          0x03, 0x02, 0x00, 0x00, 0x00,
          0x01, 0x00, 0x00, 0x00,        //   1
          0xFF, 0xFF, 0xFF, 0xFF,        //   -1
    0x0B, 0x02, 0x00, 'I','A',          // INT_ARRAY "IA" = {7,8}
          0x02, 0x00, 0x00, 0x00,
          0x07, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x0A, 0x01, 0x00, 'C',              // COMPOUND "C"
          0x01, 0x01, 0x00, 'X', 0x01,  //   BYTE "X" = 1
          0x00,                          //   end of "C"
    0x00,                               // end of root
};

// ============================================================
// Big-endian fixture (Java NBT): lengths and payloads are BE.
// ============================================================
static const uint8_t kBeRoot[] = {
    0x0A, 0x00, 0x04, 'R','o','o','t',
    0x03, 0x00, 0x01, 'I', 0x12, 0x34, 0x56, 0x78,
    0x04, 0x00, 0x01, 'L', 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x08, 0x00, 0x02, 'S','t', 0x00, 0x02, 'h','i',
    0x00,
};

// ============================================================
// Hand-written item entry: a used diamond sword with
// Sharpness III and Knockback II. Reading this must fill the
// ItemStack — including the enchantments — from raw bytes.
// ============================================================
static const uint8_t kItemEntry[] = {
    0x0A, 0x00, 0x00,                          // COMPOUND, unnamed
    0x02, 0x02, 0x00, 'i','d',  0x14, 0x01,    // SHORT "id"     = 0x0114 = 276
    0x01, 0x05, 0x00, 'C','o','u','n','t', 0x01,   // BYTE "Count" = 1
    0x02, 0x06, 0x00, 'D','a','m','a','g','e', 0x0A, 0x00,  // SHORT "Damage" = 10
    0x09, 0x04, 0x00, 'e','n','c','h',         // LIST "ench" of COMPOUND
          0x0A, 0x02, 0x00, 0x00, 0x00,        //   count = 2
          0x02, 0x02, 0x00, 'i','d',  0x09, 0x00,   //   id  = 9  (Sharpness)
          0x02, 0x03, 0x00, 'l','v','l', 0x03, 0x00,//   lvl = 3
          0x00,                                 //   end of element
          0x02, 0x02, 0x00, 'i','d',  0x0C, 0x00,   //   id  = 12 (Knockback)
          0x02, 0x05, 0x00, 'l','e','v','e','l', 0x02, 0x00, // "level" = 2
          0x00,
    0x00,                                       // end of root
};

int main() {
    printf("NBT format + item NBT\n");

    // ================================================================
    // 1. Little-endian: read every type back
    // ================================================================
    printf("\n-- format: little-endian fixture --\n");
    withRoot(kLeRoot, sizeof(kLeRoot), true, [](cursestone::nbt::Tag* root) {
        check(root != nullptr, "the fixture parses");
        if (!root) return;
        check(cursestone::nbt::type_of(root) == cursestone::nbt::TAG_COMPOUND, "the root is a compound");
        check(root->name == "Root", "the root name survives");

        check(cursestone::nbt::get_byte(root, "B", 0) == 127,    "BYTE   = 127");
        check(cursestone::nbt::get_short(root, "S", 0) == 12345,  "SHORT  = 12345");
        check(cursestone::nbt::get_int(root, "I", 0) == 0x12345678, "INT = 0x12345678");
        check(cursestone::nbt::get_long(root, "L", 0) == 0x0102030405060708LL,
              "LONG   = 0x0102030405060708");
        check(near(cursestone::nbt::get_float(root, "F", 0.0f), 1.0), "FLOAT = 1.0");
        check(near(cursestone::nbt::get_double(root, "D", 0.0), 1.5), "DOUBLE = 1.5");
        check(std::string(cursestone::nbt::get_string(root, "St", "")) == "hi",
              "STRING = \"hi\"");

        const cursestone::nbt::Tag* ba = cursestone::nbt::get(root, "BA");
        check(ba && cursestone::nbt::type_of(ba) == cursestone::nbt::TAG_BYTE_ARRAY, "BYTE_ARRAY is a byte array");
        if (ba) {
            const auto* arr = static_cast<const cursestone::nbt::ByteArrayTag*>(ba);
            check(arr->data.size() == 2 && arr->data[0] == 0xAA && arr->data[1] == 0xBB,
                  "BYTE_ARRAY bytes = {0xAA,0xBB}");
        }

        const cursestone::nbt::Tag* ia = cursestone::nbt::get(root, "IA");
        check(ia && cursestone::nbt::type_of(ia) == cursestone::nbt::TAG_INT_ARRAY, "INT_ARRAY is an int array");
        if (ia) {
            const auto* arr = static_cast<const cursestone::nbt::IntArrayTag*>(ia);
            check(arr->data.size() == 2 && arr->data[0] == 7 && arr->data[1] == 8,
                  "INT_ARRAY values = {7,8}");
        }

        const cursestone::nbt::Tag* li = cursestone::nbt::get(root, "Li");
        check(li && cursestone::nbt::type_of(li) == cursestone::nbt::TAG_LIST, "LIST is a list");
        check(li && static_cast<const cursestone::nbt::ListTag*>(li)->element_type == cursestone::nbt::TAG_INT,
              "LIST remembers its element type");
        check(li && cursestone::nbt::size(li) == 2, "LIST has 2 elements");
        if (li && cursestone::nbt::size(li) == 2) {
            const auto* e0 = static_cast<const cursestone::nbt::IntTag*>(cursestone::nbt::at(li, 0));
            const auto* e1 = static_cast<const cursestone::nbt::IntTag*>(cursestone::nbt::at(li, 1));
            check(e0 && e1 && e0->value == 1 && e1->value == -1,
                  "LIST elements are NOT named and keep their values (1, -1)");
        }

        const cursestone::nbt::Tag* c = cursestone::nbt::get_compound(root, "C");
        check(c != nullptr, "the nested compound is reachable");
        check(c && cursestone::nbt::get_byte(c, "X", 0) == 1, "the nested compound's field reads");
    });

    // ================================================================
    // 2. The writer reproduces the fixture byte for byte
    // ================================================================
    printf("\n-- format: byte-exact round trip --\n");
    withRoot(kLeRoot, sizeof(kLeRoot), true, [](cursestone::nbt::Tag* root) {
        if (!root) return;
        check(bytesEqual(serialize(root, true), kLeRoot, sizeof(kLeRoot)),
              "re-serializing the little-endian fixture is byte-identical");
    });
    withRoot(kBeRoot, sizeof(kBeRoot), false, [](cursestone::nbt::Tag* root) {
        check(root != nullptr, "the big-endian fixture parses");
        check(root && cursestone::nbt::get_int(root, "I", 0) == 0x12345678,
              "big-endian INT = 0x12345678");
        check(root && cursestone::nbt::get_long(root, "L", 0) == 0x0102030405060708LL,
              "big-endian LONG = 0x0102030405060708");
        check(root && std::string(cursestone::nbt::get_string(root, "St", "")) == "hi",
              "big-endian STRING = \"hi\"");
        if (root) {
            check(bytesEqual(serialize(root, false), kBeRoot, sizeof(kBeRoot)),
                  "re-serializing the big-endian fixture is byte-identical");
        }
    });

    // ================================================================
    // 3. Malformed input fails cleanly
    // ================================================================
    printf("\n-- format: malformed input --\n");
    {
        // Every truncation must either fail or return a tag — never read
        // past the end. The interesting ones (mid-payload) must fail.
        int parsed = 0, failed = 0;
        for (size_t n = 0; n < sizeof(kLeRoot); ++n) {
            withRoot(kLeRoot, n, true, [&](cursestone::nbt::Tag* t) {
                if (t) ++parsed; else ++failed;
            });
        }
        check(failed > 0, "truncated input is rejected, not read past the buffer");
        // Every strict prefix of a single-root stream necessarily ends
        // mid-tag, so none of them may be accepted as complete.
        check(parsed == 0, "no truncated prefix is mistaken for a complete tag");

        // Trailing bytes after a complete tag, on the other hand, are not an
        // error: a reader must stop at the end of the root, not demand EOF.
        std::vector<uint8_t> padded(kLeRoot, kLeRoot + sizeof(kLeRoot));
        padded.insert(padded.end(), 8, 0xAB);
        withRoot(padded.data(), padded.size(), true, [](cursestone::nbt::Tag* t) {
            check(t != nullptr && cursestone::nbt::get_int(t, "I", 0) == 0x12345678,
                  "trailing bytes after the root are ignored");
        });
        // A type byte the format does not define.
        const uint8_t bogus[] = { 0x0D, 0x01, 0x00, 'x', 0x00 };
        withRoot(bogus, sizeof(bogus), true,
                 [](cursestone::nbt::Tag* t) { check(t == nullptr, "an undefined tag type is rejected"); });
        // An empty buffer.
        withRoot(nullptr, 0, true,
                 [](cursestone::nbt::Tag* t) { check(t == nullptr, "an empty buffer is rejected"); });
    }

    // ================================================================
    // 4. Compound remove (the API itemStackToNbt() needs to clear "ench")
    // ================================================================
    printf("\n-- format: CompoundTag::remove --\n");
    {
        cursestone::nbt::Tag* c = new cursestone::nbt::CompoundTag();
        cursestone::nbt::set_byte(c, "a", 1);
        cursestone::nbt::set_byte(c, "b", 2);
        cursestone::nbt::set_byte(c, "c", 3);
        check(cursestone::nbt::remove(c, "b"), "removing an existing key reports true");
        check(!cursestone::nbt::contains(c, "b"), "the removed key is gone");
        check(static_cast<const cursestone::nbt::CompoundTag*>(c)->entries.size() == 2, "the size shrank");
        check(cursestone::nbt::get_byte(c, "a", 0) == 1 &&
              cursestone::nbt::get_byte(c, "c", 0) == 3, "the other keys survive");
        check(!cursestone::nbt::remove(c, "zz"), "removing a missing key reports false");
        // The survivors keep their insertion order in the serialized bytes
        // (a hole would reorder them).
        cursestone::nbt::set_name(c, "");
        const std::vector<uint8_t> out = serialize(c, true);
        check(out.size() > 0 && out[out.size() - 1] == 0x00, "the compound still ends with TAG_End");
        auto idxOf = [&out](char k) {
            for (size_t i = 0; i + 1 < out.size(); ++i)
                if (out[i] == (uint8_t)k) return i;
            return (size_t)-1;
        };
        check(idxOf('a') < idxOf('c'), "the remaining entries keep their order");
        delete c;
    }

    // ================================================================
    // 5. Item NBT: the hand-written sword entry
    // ================================================================
    printf("\n-- item NBT: reading the fixture --\n");
    {
        cursestone::nbt::Tag* entry = cursestone::nbt::read(kItemEntry, sizeof(kItemEntry), true);
        check(entry != nullptr, "the item entry parses");
        ItemStack stack;
        check(cursestone::item::itemStackFromNbt(entry, stack), "the stack is read");
        check(stack.id == 276, "id = 276 (diamond sword)");
        check(stack.count == 1, "Count = 1");
        check(stack.meta == 10, "Damage = 10 -> meta 10");
        check(stack.enchantmentLevel(EnchantmentId::ENCH_SHARPNESS) == 3,
              "\"ench\" -> Sharpness III");
        check(stack.enchantmentLevel(EnchantmentId::ENCH_KNOCKBACK) == 2,
              "\"ench\" -> Knockback II (spelled \"level\")");
        check(stack.enchantmentLevel(EnchantmentId::ENCH_FIRE_ASPECT) == 0,
              "an enchantment the item does not have reads 0");
        if (entry) delete entry;
    }

    // ================================================================
    // 6. Item NBT: round trip through the writer
    // ================================================================
    printf("\n-- item NBT: round trip --\n");
    {
        ItemStack s;
        s.id = 310;                        // diamond chestplate
        s.meta = 7;
        s.count = 1;
        s.ench.add(EnchantmentId::ENCH_PROTECTION, 4);
        s.ench.add(EnchantmentId::ENCH_THORNS, 2);

        cursestone::nbt::Tag* entry = new cursestone::nbt::CompoundTag();
        cursestone::item::itemStackToNbt(s, entry);
        check(cursestone::nbt::get_short(entry, "id", 0) == 310, "id is written as a short");
        check(cursestone::nbt::get_byte(entry, "Count", 0) == 1, "Count is written as a byte");
        check(cursestone::nbt::get_short(entry, "Damage", 0) == 7, "Damage is written as a short");

        ItemStack back;
        check(cursestone::item::itemStackFromNbt(entry, back), "the written entry reads back");
        check(back.id == 310 && back.count == 1 && back.meta == 7, "id/count/meta survive");
        check(back.enchantmentLevel(EnchantmentId::ENCH_PROTECTION) == 4,
              "Protection IV survives the round trip");
        check(back.enchantmentLevel(EnchantmentId::ENCH_THORNS) == 2,
              "Thorns II survives the round trip");

        // Writing over an entry that already had enchantments must not leave
        // the old list behind.
        ItemStack plain;
        plain.id = 1; plain.count = 64;
        cursestone::item::itemStackToNbt(plain, entry);
        check(!cursestone::nbt::contains(entry, "ench"),
              "a plain stack clears a stale \"ench\" from the entry");
        delete entry;
    }

    // ================================================================
    // 7. Item NBT: a plain item keeps the legacy byte layout
    // ================================================================
    printf("\n-- item NBT: no extra bytes for a plain item --\n");
    {
        ItemStack plain;
        plain.id = 5; plain.count = 12; plain.meta = 3;

        cursestone::nbt::Tag* viaModule = new cursestone::nbt::CompoundTag();
        cursestone::item::itemStackToNbt(plain, viaModule);
        cursestone::nbt::set_name(viaModule, "");
        const std::vector<uint8_t> a = serialize(viaModule, true);
        delete viaModule;

        // The exact three setters every loader used before this module.
        cursestone::nbt::Tag* legacy = new cursestone::nbt::CompoundTag();
        cursestone::nbt::set_short(legacy, "id", (int16_t)5);
        cursestone::nbt::set_byte(legacy, "Count", (int8_t)12);
        cursestone::nbt::set_short(legacy, "Damage", (int16_t)3);
        cursestone::nbt::set_name(legacy, "");
        const std::vector<uint8_t> b = serialize(legacy, true);
        delete legacy;

        check(!a.empty() && a == b, "an unenchanted item serializes exactly as before");
    }

    // ================================================================
    // 8. Item NBT: entries that hold nothing
    // ================================================================
    printf("\n-- item NBT: empty / invalid entries --\n");
    {
        ItemStack out;
        out.id = 99; out.count = 5;   // a failed read must clear this

        cursestone::nbt::Tag* idZero = new cursestone::nbt::CompoundTag();
        cursestone::nbt::set_short(idZero, "id", 0);
        cursestone::nbt::set_byte(idZero, "Count", 4);
        // A failed read leaves `out` EMPTY (see the header contract), so a
        // caller that reused a stack cannot keep the old item.
        check(!cursestone::item::itemStackFromNbt(idZero, out) && out.isEmpty(),
              "id = 0 is not an item, and the output is cleared");
        delete idZero;

        cursestone::nbt::Tag* countZero = new cursestone::nbt::CompoundTag();
        cursestone::nbt::set_short(countZero, "id", 5);
        cursestone::nbt::set_byte(countZero, "Count", 0);
        check(!cursestone::item::itemStackFromNbt(countZero, out), "Count = 0 is not an item");
        delete countZero;

        cursestone::nbt::Tag* empty = new cursestone::nbt::CompoundTag();
        check(!cursestone::item::itemStackFromNbt(empty, out), "an empty compound is not an item");
        delete empty;

        check(!cursestone::item::itemStackFromNbt(nullptr, out), "a null entry is not an item");

        cursestone::nbt::Tag* notCompound = new cursestone::nbt::ByteTag(1);
        check(!cursestone::item::itemStackFromNbt(notCompound, out),
              "a non-compound entry is not an item");
        delete notCompound;

        // A garbage "ench" (wrong element type / missing keys / level 0)
        // must not produce phantom enchantments.
        cursestone::nbt::Tag* junk = new cursestone::nbt::CompoundTag();
        cursestone::nbt::set_short(junk, "id", 5);
        cursestone::nbt::set_byte(junk, "Count", 1);
        cursestone::nbt::Tag* badList = new cursestone::nbt::ListTag("ench", cursestone::nbt::TAG_INT);
        cursestone::nbt::append(badList, new cursestone::nbt::IntTag(9));
        cursestone::nbt::set(junk, "ench", badList);
        ItemStack junkStack;
        check(cursestone::item::itemStackFromNbt(junk, junkStack) &&
              junkStack.ench.entries().empty(),
              "a non-compound 'ench' list yields no enchantments");
        delete junk;
    }

    // ================================================================
    // 10. The real storage shape: an "Items" list, through bytes
    // ================================================================
    // What TileChest / TileFurnace / Player::saveData actually write: a
    // per-slot compound { Slot, <item fields>, ench }, inside an "Items"
    // list, inside the tile/player root — and then read back after a full
    // serialize/parse cycle, the way a restart does it.
    printf("\n-- container storage round trip --\n");
    {
        ItemStack stored;
        stored.id = 310; stored.count = 1; stored.meta = 12;
        stored.ench.add(EnchantmentId::ENCH_PROTECTION, 2);
        stored.ench.add(EnchantmentId::ENCH_FIRE_PROTECTION, 1);

        cursestone::nbt::Tag* root = new cursestone::nbt::CompoundTag("Chest");
        cursestone::nbt::Tag* items = new cursestone::nbt::ListTag("Items", cursestone::nbt::TAG_COMPOUND);
        cursestone::nbt::Tag* entry = new cursestone::nbt::CompoundTag();
        cursestone::nbt::set_byte(entry, "Slot", (int8_t)13);
        cursestone::item::itemStackToNbt(stored, entry);
        cursestone::nbt::append(items, entry);
        cursestone::nbt::set(root, "Items", items);

        std::vector<uint8_t> bytes;
        cursestone::nbt::write(root, true, bytes);
        delete root;
        check(!bytes.empty(), "the container root serializes");

        cursestone::nbt::Tag* back = cursestone::nbt::read(bytes.data(), bytes.size(), true);
        check(back != nullptr, "the container root parses back");
        if (back) {
            const cursestone::nbt::Tag* list = cursestone::nbt::get(back, "Items");
            check(list && cursestone::nbt::size(list) == 1, "the Items list has one entry");
            const cursestone::nbt::Tag* e = cursestone::nbt::at(list, 0);
            check(cursestone::nbt::get_byte(e, "Slot", -1) == 13, "the slot number survives");
            ItemStack read;
            check(cursestone::item::itemStackFromNbt(e, read), "the stored stack reads back");
            check(read.id == 310 && read.count == 1 && read.meta == 12,
                  "the item fields survive a real save/load cycle");
            check(read.enchantmentLevel(EnchantmentId::ENCH_PROTECTION) == 2 &&
                  read.enchantmentLevel(EnchantmentId::ENCH_FIRE_PROTECTION) == 1,
                  "both enchantments survive a real save/load cycle");
            delete back;
        }
    }

    // ================================================================
    // 11. Custom display-name NBT survives persistence (the "sword
    //     came back nameless after a relog" regression)
    // ================================================================
    // ItemStack::nbt (the {display:{Name}} wire blob a plugin gave the
    // stack) must round-trip through itemStackToNbt/itemStackFromNbt —
    // the path Player::saveData / TileChest / TileFurnace share —
    // byte-identically, riding the vanilla "tag" compound.
    printf("\n-- custom item name persistence --\n");
    {
        ItemStack named;
        named.id = 276; named.count = 1; named.meta = 0;
        named.nbt = cursestone::item::buildDisplayNbt("\xc2\xa7 6Espada VIP");
        check(!named.nbt.empty(), "buildDisplayNbt produces a blob");

        cursestone::nbt::Tag* root = new cursestone::nbt::CompoundTag("Player");
        cursestone::nbt::Tag* items = new cursestone::nbt::ListTag("Inventory", cursestone::nbt::TAG_COMPOUND);
        cursestone::nbt::Tag* entry = new cursestone::nbt::CompoundTag();
        cursestone::nbt::set_byte(entry, "Slot", (int8_t)0);
        cursestone::item::itemStackToNbt(named, entry);
        cursestone::nbt::append(items, entry);
        cursestone::nbt::set(root, "Inventory", items);

        // The blob landed as a real "tag" compound child of the entry.
        const cursestone::nbt::Tag* tag = cursestone::nbt::get(entry, "tag");
        check(tag && cursestone::nbt::type_of(tag) == cursestone::nbt::TAG_COMPOUND,
              "the network NBT is stored as the vanilla \"tag\" compound");
        const cursestone::nbt::Tag* display = tag ? cursestone::nbt::get(tag, "display") : nullptr;
        check(display && cursestone::nbt::type_of(display) == cursestone::nbt::TAG_COMPOUND,
              "\"tag\" holds the \"display\" compound");
        check(display && std::string(cursestone::nbt::get_string(display, "Name", "")) ==
                            "\xc2\xa7 6Espada VIP",
              "\"display.Name\" carries the custom name");

        // Full save/load cycle: serialize the root the way the storage
        // does, parse it back, read the stack.
        std::vector<uint8_t> bytes;
        cursestone::nbt::write(root, true, bytes);
        delete root;
        check(!bytes.empty(), "the player root serializes");
        cursestone::nbt::Tag* back = cursestone::nbt::read(bytes.data(), bytes.size(), true);
        check(back != nullptr, "the player root parses back");
        if (back) {
            const cursestone::nbt::Tag* list = cursestone::nbt::get(back, "Inventory");
            const cursestone::nbt::Tag* e = (list && cursestone::nbt::size(list) > 0)
                                     ? cursestone::nbt::at(list, 0) : nullptr;
            check(e != nullptr, "the inventory entry is present after reload");
            ItemStack read;
            check(cursestone::item::itemStackFromNbt(e, read), "the named stack reads back");
            check(read.id == 276, "the item fields survive");
            check(read.nbt == named.nbt,
                  "the display-name blob survives a real save/load cycle byte-identically");
            delete back;
        }

        // Rewriting the entry with a plain stack must clear the stale
        // "tag" (otherwise a renamed-over item would keep the old name).
        cursestone::nbt::Tag* entry2 = new cursestone::nbt::CompoundTag();
        cursestone::item::itemStackToNbt(named, entry2);
        check(cursestone::nbt::contains(entry2, "tag"), "the entry now has a tag");
        ItemStack plain;
        plain.id = 1; plain.count = 64;
        cursestone::item::itemStackToNbt(plain, entry2);
        check(!cursestone::nbt::contains(entry2, "tag"),
              "a plain stack clears a stale \"tag\" from the entry");
        delete entry2;
    }

    printf("\n%s (%d failure(s))\n",
           failures ? "NBT TEST: FAIL" : "NBT TEST: OK", failures);
    return failures ? 1 : 0;
}
