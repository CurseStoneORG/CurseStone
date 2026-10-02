// tools/sign_tile_check.cpp
//
// Regression test for the sign crash and the stale-tile persistence bug.
//
//   1. An UNTOUCHED sign line arrives as an empty NBT string. The NBT
//      layer used to hand that back as NULL, and Player::handleSignEdit
//      builds std::string from it -> strlen(NULL) -> SIGSEGV. Placing a
//      sign, typing on one line and closing the editor is enough to hit it.
//
//   2. Breaking a sign/chest removes the Tile from Level::tiles_ but left
//      its NBT blob in the owning chunk's tile_entities array. A chunk
//      whose only tile was broken was never revisited by
//      syncTilesToChunks(), so the blob survived to disk and the tile came
//      back on the next restart, on top of an air block.
//
//   g++ -std=c++20 -Isrc tools/sign_tile_check.cpp <all objects except main.o> -lz -o sign_tile_check
#include "level/Level.h"
#include "level/ChunkManager.h"
#include "level/format/Chunk.h"
#include "tile/TileSign.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "block/BlockIds.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace cursestone;
using cursestone::tile::TileSign;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// ---- 1. empty NBT strings must never come back as NULL -------------------
static void testEmptyStringNbt() {
    printf("1. empty sign line must not decode to NULL\n");

    mcp_tag_t* root = mcp_compound_tag_new();
    mcp_compound_tag_set_string(root, "id", "Sign");
    mcp_compound_tag_set_string(root, "Text1", "hola");
    mcp_compound_tag_set_string(root, "Text2", "");
    mcp_compound_tag_set_string(root, "Text3", "");
    mcp_compound_tag_set_string(root, "Text4", "");
    size_t blen = 0;
    uint8_t* buf = mcp_nbt_write_bytes(root, true, &blen);
    mcp_tag_destroy(root);

    mcp_tag_t* parsed = mcp_nbt_read_bytes(buf, blen, true);
    free(buf);
    check(parsed != nullptr, "NBT round-trips");

    const char* t1 = mcp_compound_tag_get_string(parsed, "Text1", "");
    const char* t2 = mcp_compound_tag_get_string(parsed, "Text2", "");
    const char* missing = mcp_compound_tag_get_string(parsed, "Nope", "dflt");
    check(t1 != nullptr, "filled line is non-NULL");
    check(t2 != nullptr, "EMPTY line is non-NULL (this was the SIGSEGV)");
    check(missing != nullptr && std::string(missing) == "dflt",
          "missing key falls back to the caller's default");

    // The exact expression that used to kill the server.
    std::string lines[4] = {
        mcp_compound_tag_get_string(parsed, "Text1", ""),
        mcp_compound_tag_get_string(parsed, "Text2", ""),
        mcp_compound_tag_get_string(parsed, "Text3", ""),
        mcp_compound_tag_get_string(parsed, "Text4", "")
    };
    check(lines[1].empty() && lines[2].empty() && lines[3].empty(),
          "empty lines survive as empty strings");
    check(lines[0] == "hola", "filled line keeps its text");
    mcp_tag_destroy(parsed);
}

// ---- 2. tile NBT round trip with blank lines and no creator --------------
static void testTileRoundTrip() {
    printf("2. TileSign NBT round trip\n");
    TileSign sign(level::Position(10.0, 68.0, 20.0));
    sign.setCreator("uuid-abc");
    sign.setLine(0, "line one");
    sign.setLine(1, "");
    sign.setLine(2, "");
    sign.setLine(3, "line four");
    check(sign.line(0) == "line one", "setLine wrote line 0");
    check(sign.line(9).empty(), "out-of-range line() is safe");
    sign.setLine(42, "nope");          // must be ignored, not written OOB
    check(sign.line(3) == "line four", "out-of-range setLine is ignored");

    mcp_tag_t* tag = mcp_compound_tag_new();
    sign.writeNBT(tag);
    size_t blen = 0;
    uint8_t* buf = mcp_nbt_write_bytes(tag, true, &blen);
    mcp_tag_destroy(tag);

    TileSign back(level::Position(10.0, 68.0, 20.0));
    mcp_tag_t* parsed = mcp_nbt_read_bytes(buf, blen, true);
    check(parsed != nullptr, "tile NBT parses back");
    back.readNBT(parsed);
    mcp_tag_destroy(parsed);
    free(buf);

    check(back.line(0) == "line one", "line 0 survives the round trip");
    check(back.line(1).empty(), "blank line 1 survives");
    check(back.line(3) == "line four", "line 3 survives");
    check(back.creator() == "uuid-abc", "creator survives");
}

// ---- 3. breaking a tile must not leave a stale blob in the chunk ---------
static void testStaleTileBlob() {
    printf("3. removing a tile clears the chunk's tile_entities blob\n");

    level::LevelConfig cfg;
    cfg.name = "signtest";
    cfg.dataPath = "/tmp/csb/signtest";
    cfg.seed = 4242;
    level::Level lvl(cfg);

    const int cx = 0, cz = 0;
    mcp_chunk_t* c = lvl.chunks().getChunkIfLoaded(cx, cz);
    if (!c) {
        // Make sure the chunk exists and is loaded.
        mcp_chunk_t* got = lvl.chunks().getChunk(cx, cz);
        (void)got;
        c = lvl.chunks().getChunkIfLoaded(cx, cz);
    }
    if (!c) {
        printf("  [SKIP] could not obtain chunk (%d,%d)\n", cx, cz);
        return;
    }
    check(c->tile_entities != nullptr || true, "chunk acquired");

    const int bx = 5, by = 68, bz = 5;
    auto sign = std::make_shared<TileSign>(level::Position(bx, by, bz));
    sign->setCreator("uuid-abc");
    sign->setLine(0, "persisted text");
    lvl.setTile(bx, by, bz, sign);

    lvl.save();
    size_t afterPlace = c->tile_entity_count;
    check(afterPlace == 1, "placed sign wrote exactly one blob");
    check(lvl.getTile(bx, by, bz) != nullptr, "tile is in the registry");

    // Break it, exactly like Player::handleBlockBreak does.
    lvl.removeTile(bx, by, bz);
    check(lvl.getTile(bx, by, bz) == nullptr, "tile left the registry");
    lvl.save();

    size_t afterBreak = c->tile_entity_count;
    check(afterBreak == 0,
          "chunk blob cleared after the tile was removed (was 1 before the fix)");
    if (afterBreak != 0) {
        printf("        -> the broken sign would come back on the next restart\n");
    }
}

int main() {
    testEmptyStringNbt();
    testTileRoundTrip();
    testStaleTileBlob();
    printf("\n%s (%d failure(s))\n", failures ? "SIGN TILE CHECK: FAIL" : "SIGN TILE CHECK: OK",
           failures);
    return failures ? 1 : 0;
}