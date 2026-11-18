// Registry equivalence check for the per-mob EntityTypes split.
// Compares the new registry against the table the previous
// EntityTypes.cpp shipped (ids, names, aliases, invalid handling).
#include "entity/EntityTypes.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace cursestone::entity;

struct Expect {
    int32_t id;
    const char* name;
};

static const Expect kExpected[] = {
    {10, "chicken"}, {11, "cow"}, {12, "pig"}, {13, "sheep"}, {14, "wolf"},
    {15, "villager"}, {16, "mooshroom"}, {17, "squid"}, {18, "rabbit"},
    {19, "bat"}, {22, "ocelot"}, {23, "horse"}, {24, "donkey"}, {25, "mule"},
    {26, "skeleton_horse"}, {27, "zombie_horse"}, {32, "zombie"}, {33, "creeper"},
    {34, "skeleton"}, {35, "spider"}, {36, "zombie_pigman"}, {37, "slime"},
    {38, "enderman"}, {39, "silverfish"}, {40, "ghast"}, {41, "lava_slime"},
    {42, "magma_cube"}, {43, "blaze"}, {44, "zombie_villager"}, {45, "witch"},
    {46, "stray"}, {47, "husk"}, {48, "wither_skeleton"},
};

int main() {
    int failures = 0;
    const size_t n = sizeof(kExpected) / sizeof(kExpected[0]);

    // 1. Every expected id is valid with the exact canonical name.
    for (const Expect& e : kExpected) {
        if (!isValidMobType(e.id)) {
            printf("FAIL id %d not valid\n", e.id);
            ++failures;
        }
        const char* nm = mobTypeName(e.id);
        if (!nm || std::strcmp(nm, e.name) != 0) {
            printf("FAIL name of %d: got %s want %s\n", e.id, nm ? nm : "(null)", e.name);
            ++failures;
        }
        int32_t back = 0;
        if (!mobTypeFromName(e.name, &back) || back != e.id) {
            printf("FAIL fromName(%s) != %d\n", e.name, e.id);
            ++failures;
        }
        std::string upper = e.name;
        for (char& c : upper) c = (char)std::toupper((unsigned char)c);
        if (!mobTypeFromName(upper, &back) || back != e.id) {
            printf("FAIL case-insensitive %s\n", e.name);
            ++failures;
        }
    }

    // 2. Count and out-of-range handling unchanged.
    int valid = 0;
    for (int32_t id = 0; id < 256; ++id)
        if (isValidMobType(id))
            ++valid;
    if (valid != (int)n) {
        printf("FAIL valid count %d != %zu\n", valid, n);
        ++failures;
    }
    for (int32_t bad : {-5, 0, 9, 20, 21, 28, 29, 30, 31, 49, 100, 255}) {
        if (isValidMobType(bad)) {
            printf("FAIL id %d must be invalid\n", bad);
            ++failures;
        }
        if (mobTypeName(bad) != nullptr) {
            printf("FAIL name of invalid %d not null\n", bad);
            ++failures;
        }
        if (mobTypeKind(bad) != MobKind::Unknown) {
            printf("FAIL kind of invalid %d not Unknown\n", bad);
            ++failures;
        }
    }

    // 3. The alias set of the previous table resolves identically.
    struct Alias {
        const char* name;
        int32_t id;
    };
    static const Alias kAliases[] = {
        {"mushroom_cow", 16}, {"pigman", 36}, {"zombie_pig", 36},
        {"magma", 42}, {"lava_cube", 41}, {"cat", 22},
        {"MUSHROOM_COW", 16}, {"PIGMAN", 36},
    };
    for (const Alias& a : kAliases) {
        int32_t id = 0;
        if (!mobTypeFromName(a.name, &id) || id != a.id) {
            printf("FAIL alias %s -> %d\n", a.name, a.id);
            ++failures;
        }
    }
    // The old doc string mentioned "snowman"; the old table never had it.
    int32_t id = 0;
    if (mobTypeFromName("snowman", &id)) {
        printf("FAIL snowman must not resolve\n");
        ++failures;
    }

    // 4. mobTypeKind facts for spot-checked mobs.
    if (mobTypeKind(10) != MobKind::Passive || mobTypeKind(32) != MobKind::Hostile ||
        mobTypeKind(14) != MobKind::Neutral || mobTypeKind(17) != MobKind::Water ||
        mobTypeKind(19) != MobKind::Ambient) {
        printf("FAIL mobTypeKind facts\n");
        ++failures;
    }

    if (failures == 0)
        printf("OK: %zu mobs, aliases and invalid handling match the previous registry\n", n);
    return failures == 0 ? 0 : 1;
}
