// tools/sign_nbt_check.cpp
//
// Reproduces the SIGSEGV that happened when a sign was placed, typed into
// and then broken: Player::handleSignEdit() builds
//
//     std::array<std::string,4> lines = {
//         mcp_compound_tag_get_string(root, "Text1", ""), ... };
//
// straight out of the client's BlockEntityData NBT. A sign the player only
// typed one line into has "" for the other three, and this test shows what
// the NBT layer hands back for an empty string.
//
//   g++ -std=c++20 -Isrc tools/sign_nbt_check.cpp <nbt objects> -o sign_nbt_check
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/StringTag.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


int main() {
    // Build exactly the NBT a 0.15.10 client sends when it closes the sign
    // editor after typing only on the first line.
    mcp_tag_t* root = mcp_compound_tag_new();
    mcp_compound_tag_set_string(root, "id", "Sign");
    mcp_compound_tag_set_int(root, "x", -418);
    mcp_compound_tag_set_int(root, "y", 68);
    mcp_compound_tag_set_int(root, "z", 2135);
    mcp_compound_tag_set_string(root, "Text1", "hola");
    mcp_compound_tag_set_string(root, "Text2", "");   // <-- untouched line
    mcp_compound_tag_set_string(root, "Text3", "");
    mcp_compound_tag_set_string(root, "Text4", "");

    size_t blen = 0;
    uint8_t* buf = mcp_nbt_write_bytes(root, /*le=*/true, &blen);
    mcp_tag_destroy(root);
    if (!buf) { printf("could not serialise NBT\n"); return 1; }

    // ...and read it back the way handleSignEdit does.
    mcp_tag_t* parsed = mcp_nbt_read_bytes(buf, blen, /*le=*/true);
    free(buf);
    if (!parsed) { printf("could not parse NBT\n"); return 1; }

    const char* id    = mcp_compound_tag_get_string(parsed, "id", "");
    const char* text1 = mcp_compound_tag_get_string(parsed, "Text1", "");
    const char* text2 = mcp_compound_tag_get_string(parsed, "Text2", "");
    const char* text4 = mcp_compound_tag_get_string(parsed, "Text4", "");

    printf("id    = %s\n", id    ? id    : "(NULL)");
    printf("Text1 = %s\n", text1 ? text1 : "(NULL)");
    printf("Text2 = %s\n", text2 ? text2 : "(NULL)");
    printf("Text4 = %s\n", text4 ? text4 : "(NULL)");

    const bool ok = id && text1 && text2 && text4;
    printf("\nall four lookups non-NULL: %s\n", ok ? "yes" : "NO");

    // This is the exact line that crashed. Building a std::string from a
    // NULL const char* calls strlen(NULL).
    printf("building std::string from Text2 ...\n");
    fflush(stdout);
    std::string line2 = text2;
    printf("survived, Text2=\"%s\"\n", line2.c_str());

    mcp_tag_destroy(parsed);
    return ok ? 0 : 1;
}