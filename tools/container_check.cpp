// tools/container_check.cpp
//
// Regression test for the three bugs reported against the village/structure
// containers, plus the furnace cooking loop:
//
//   1. "Village chests are invisible" — a chest that arrives with the WORLD
//      (a generator piece, a dungeon, a structure) used to exist only as a
//      block, never as a block entity. MCPE 0.15 draws and opens chests and
//      furnaces through their block actor, so the player walked over an
//      unknown-but-solid block, tapping it did nothing, and the log still
//      said "opened chest". Level::hydrateTilesFromBlocks() now builds the
//      missing tiles the first time a chunk is touched.
//
//   2. "Tapping a generated furnace kicks the client" — same root cause:
//      the container UI was opened with no furnace block entity behind it.
//      openChestAt()/openFurnaceAt() now send BlockEntityDataPacket before
//      the rest of the window, and the window id comes from the same 2..98
//      range Infinity hands out (the old fixed id 1 is never used by the
//      reference server).
//
//   3. Cooking — TileFurnace::tick() lights fuel, smelts the input through
//      the CraftingManager and moves the block between FURNACE (61) and
//      BURNING_FURNACE (62) so the client sees the flame.
//
//   g++ -std=c++20 -Isrc tools/container_check.cpp
//       <every object except main.o> -lz -pthread -o container_check
#include "Server.h"
#include "Player.h"
#include "block/BlockIds.h"
#include "inventory/CraftingManager.h"
#include "item/ItemConstants.h"
#include "item/ItemStack.h"
#include "level/ChunkManager.h"
#include "level/Level.h"
#include "level/format/Chunk.h"
#include "tile/TileChest.h"
#include "tile/TileFactory.h"
#include "tile/TileFurnace.h"
#include "tile/TileSign.h"
#include "tile/TileSpawner.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <string>

using namespace cursestone;
namespace blk = cursestone::block;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* what) {
    ++checks;
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Item ids used below that have no constant in ItemConstants.h.
static constexpr uint16_t ITEM_COAL = 263;

static mcp_chunk_t* loadedChunk(level::Level& lvl, int cx, int cz) {
    if (!lvl.chunks().getChunkIfLoaded(cx, cz))
        (void)lvl.chunks().getChunk(cx, cz);
    return lvl.chunks().getChunkIfLoaded(cx, cz);
}

// Put a raw block into a chunk. This is exactly what the structure
// generator does: it writes blocks with no block entity attached.
static bool putBlock(level::Level& lvl, int x, int y, int z,
                     uint8_t id, uint8_t data) {
    mcp_chunk_t* c = loadedChunk(lvl, x >> 4, z >> 4);
    if (!c) return false;
    mcp_chunk_set_block_full(c, x & 15, y, z & 15, id, data);
    return true;
}

static level::LevelConfig makeConfig(const char* name, int64_t seed) {
    level::LevelConfig cfg;
    cfg.name = name;
    cfg.dataPath = std::string("/tmp/csb/") + name;
    cfg.seed = seed;
    return cfg;
}

// ---- 1. the block-id -> tile mapping itself -------------------------------
static void testTileFactory() {
    printf("1. TileFactory maps every block entity kind\n");
    level::Position p(1.0, 2.0, 3.0);

    auto chest = tile::createTileForBlock((uint8_t)blk::BlockId::CHEST, p);
    check(chest != nullptr &&
              std::dynamic_pointer_cast<tile::TileChest>(chest) != nullptr,
          "CHEST (54) builds a TileChest");

    auto furnace = tile::createTileForBlock((uint8_t)blk::BlockId::FURNACE, p);
    check(furnace != nullptr &&
              std::dynamic_pointer_cast<tile::TileFurnace>(furnace) != nullptr,
          "FURNACE (61) builds a TileFurnace");

    auto lit =
        tile::createTileForBlock((uint8_t)blk::BlockId::BURNING_FURNACE, p);
    check(lit != nullptr &&
              std::dynamic_pointer_cast<tile::TileFurnace>(lit) != nullptr,
          "BURNING_FURNACE (62) builds the same TileFurnace");

    auto sign = tile::createTileForBlock((uint8_t)blk::BlockId::SIGN_POST, p);
    check(sign != nullptr &&
              std::dynamic_pointer_cast<tile::TileSign>(sign) != nullptr,
          "SIGN_POST (63) builds a TileSign");

    auto wall = tile::createTileForBlock((uint8_t)blk::BlockId::WALL_SIGN, p);
    check(wall != nullptr &&
              std::dynamic_pointer_cast<tile::TileSign>(wall) != nullptr,
          "WALL_SIGN (68) builds a TileSign");

    auto spawner =
        tile::createTileForBlock((uint8_t)blk::BlockId::MOB_SPAWNER, p);
    check(spawner != nullptr &&
              std::dynamic_pointer_cast<tile::TileSpawner>(spawner) != nullptr,
          "MOB_SPAWNER (52) builds a TileSpawner");

    check(tile::createTileForBlock((uint8_t)blk::BlockId::STONE, p) == nullptr,
          "STONE builds no tile (common case, must stay null)");
    check(tile::isTileBlock((uint8_t)blk::BlockId::CHEST) &&
              tile::isTileBlock((uint8_t)blk::BlockId::BURNING_FURNACE),
          "isTileBlock agrees with createTileForBlock for the ids we scan");
    check(!tile::isTileBlock((uint8_t)blk::BlockId::STONE),
          "isTileBlock rejects plain blocks so the chunk scan stays cheap");
}

// ---- 2. a bare chest/furnace BLOCK gets its block entity ------------------
// This is the reported bug: before the fix getTile() returned nullptr here,
// which is what made the chest invisible and unusable.
static void testHydration() {
    printf("2. a generated chest/furnace block gets its block entity\n");
    auto cfg = makeConfig("contcheck_hydrate", 4242);
    level::Level lvl(cfg);

    // Generator-style: raw blocks, no tiles anywhere.
    const int y = 100, z = 4;
    const int chestX = 3, furnaceX = 5, litX = 6, signX = 7;
    if (!putBlock(lvl, chestX, y, z, (uint8_t)blk::BlockId::CHEST, 3) ||
        !putBlock(lvl, furnaceX, y, z, (uint8_t)blk::BlockId::FURNACE, 2) ||
        !putBlock(lvl, litX, y, z, (uint8_t)blk::BlockId::BURNING_FURNACE, 2) ||
        !putBlock(lvl, signX, y, z, (uint8_t)blk::BlockId::SIGN_POST, 0)) {
        printf("  [FAIL] could not obtain chunk (0,0)\n");
        ++failures;
        return;
    }
    // The very first read of the chunk is what triggers the hydration scan,
    // so there is no "before" state to observe here: the meaningful
    // assertion is what getTile() hands back.
    auto chest = lvl.getTile(chestX, y, z);
    check(chest != nullptr,
          "getTile() on a bare CHEST block returns a tile (was nullptr)");
    check(chest && std::dynamic_pointer_cast<tile::TileChest>(chest),
          "  ...and it is a TileChest");

    auto furnace = lvl.getTile(furnaceX, y, z);
    check(furnace && std::dynamic_pointer_cast<tile::TileFurnace>(furnace),
          "a bare FURNACE block gets a TileFurnace");

    auto lit = lvl.getTile(litX, y, z);
    check(lit && std::dynamic_pointer_cast<tile::TileFurnace>(lit),
          "a bare BURNING_FURNACE block gets a TileFurnace");

    auto sign = lvl.getTile(signX, y, z);
    check(sign && std::dynamic_pointer_cast<tile::TileSign>(sign),
          "a bare SIGN_POST block gets a TileSign");

    // This list is what Player::sendChunkTiles() walks to send the
    // BlockEntityDataPacket when a player comes into view, so it has to
    // already contain the generated containers.
    check(lvl.getTilesInChunk(0, 0).size() == 4,
          "getTilesInChunk() lists all 4 generated block entities");
    check(lvl.getTile(2, y, 4) == nullptr,
          "air next to them still has no tile");

    lvl.save();
}

// ---- 2b. asking about a chunk before it loads must not disable hydration --
// ensureTilesLoaded() must not treat "the chunk was not loaded yet" as
// "I already looked at it": if it did, the first getTile() for a chunk
// nobody has walked into yet would burn the one chance that chunk had to
// hydrate its containers, and the invisible-chest bug would come back for
// every chunk reached that way.
static void testLazyHydration() {
    printf("2b. a chunk asked about before it loads still gets hydrated\n");
    auto cfg = makeConfig("contcheck_lazy", 5);
    level::Level lvl(cfg);

    const int x = 4, y = 100, z = 4;
    // Chunk (0,0) has never been loaded in this Level.
    check(!lvl.isChunkLoaded(0, 0), "chunk (0,0) starts unloaded");
    check(lvl.getTile(x, y, z) == nullptr,
          "getTile() on an unloaded chunk returns null instead of generating");

    // The chunk gets generated afterwards, exactly like a player walking
    // into view makes the ChunkManager generate it.
    check(putBlock(lvl, x, y, z, (uint8_t)blk::BlockId::CHEST, 0),
          "the chunk loaded and the chest block was generated");
    check(lvl.isChunkLoaded(0, 0), "chunk (0,0) is loaded now");

    auto t = lvl.getTile(x, y, z);
    check(t && std::dynamic_pointer_cast<tile::TileChest>(t),
          "the chest is still hydrated after that early miss");
}

// ---- 3. hydration survives a restart (the chunk came from disk) ----------
static void testHydrationAfterReload() {
    printf("3. block entities come back when the chunk is loaded from disk\n");

    {
        auto cfg = makeConfig("contcheck_reload", 4242);
        level::Level writer(cfg);
        // A chest written to disk with NO tile blob, exactly like a village
        // chunk generated by a build from before this fix.
        const int y = 100, z = 1;
        if (!putBlock(writer, 1, y, z, (uint8_t)blk::BlockId::CHEST, 0) ||
            !putBlock(writer, 2, y, z, (uint8_t)blk::BlockId::FURNACE, 0)) {
            printf("  [FAIL] could not obtain chunk\n");
            ++failures;
            return;
        }
        writer.save();
        mcp_chunk_t* c = loadedChunk(writer, 0, 0);
        check(c && c->tile_entity_count == 0,
              "the saved chunk carries the containers as plain blocks only");
    }

    // A second Level over the same directory is a server restart.
    auto cfg2 = makeConfig("contcheck_reload", 4242);
    level::Level restarted(cfg2);
    const int y = 100, z = 1;
    // getBlockId() only reads chunks that are already loaded (it must not
    // trigger generation), so pull the chunk in from disk first — that is
    // exactly what a player walking into view does.
    check(loadedChunk(restarted, 0, 0) != nullptr,
          "chunk (0,0) was read back from the region file");
    check(restarted.getBlockId(1, y, z) == (uint8_t)blk::BlockId::CHEST,
          "the chest block itself survived the save");
    check(restarted.getBlockId(2, y, z) == (uint8_t)blk::BlockId::FURNACE,
          "the furnace block itself survived the save");

    auto t = restarted.getTile(1, y, z);
    check(t && std::dynamic_pointer_cast<tile::TileChest>(t),
          "after the restart the chest block is hydrated into a TileChest");
    auto f = restarted.getTile(2, y, z);
    check(f && std::dynamic_pointer_cast<tile::TileFurnace>(f),
          "after the restart the furnace block is hydrated into a TileFurnace");
}

// ---- 4. hydration must not clobber a live tile ---------------------------
// A chest the player already filled, or a furnace that is mid-smelt, must
// not be replaced by an empty one just because the scan ran.
static void testNoClobber() {
    printf("4. hydration never overwrites a tile that already exists\n");
    auto cfg = makeConfig("contcheck_noclobber", 99);
    level::Level lvl(cfg);

    const int y = 100, z = 2;
    const int chestX = 1, furnaceX = 3;
    if (!putBlock(lvl, chestX, y, z, (uint8_t)blk::BlockId::CHEST, 0) ||
        !putBlock(lvl, furnaceX, y, z, (uint8_t)blk::BlockId::FURNACE, 0)) {
        printf("  [FAIL] could not obtain chunk\n");
        ++failures;
        return;
    }

    // Register the live tiles BEFORE the first getTile(), which is when the
    // hydration scan runs.
    auto chest = std::make_shared<tile::TileChest>(
        level::Position((double)chestX, (double)y, (double)z));
    item::ItemStack gold{(uint16_t)cursestone::item::GOLD_INGOT, 0, 5};
    chest->setItem(0, gold);
    lvl.setTile(chestX, y, z, chest);

    auto furnace = std::make_shared<tile::TileFurnace>(
        level::Position((double)furnaceX, (double)y, (double)z));
    furnace->setItem(tile::TileFurnace::SLOT_INPUT,
                     item::ItemStack{(uint16_t)cursestone::item::COBBLE, 0, 3});
    lvl.setTile(furnaceX, y, z, furnace);

    check(lvl.getTile(chestX, y, z) == chest,
          "the pre-registered chest object survives hydration (same pointer)");
    check(lvl.getTile(furnaceX, y, z) == furnace,
          "the pre-registered furnace object survives hydration");

    auto back = std::dynamic_pointer_cast<tile::TileChest>(
        lvl.getTile(chestX, y, z));
    check(back && back->getItem(0).id == gold.id &&
              back->getItem(0).count == gold.count,
          "its contents are untouched (5 gold ingots still there)");
    auto backF = std::dynamic_pointer_cast<tile::TileFurnace>(
        lvl.getTile(furnaceX, y, z));
    check(backF && backF->getItem(tile::TileFurnace::SLOT_INPUT).count == 3,
          "the furnace input stack is untouched");

    lvl.save();
}

// ---- 5. a broken chest must not come back -------------------------------
static void testNoResurrect() {
    printf("5. a broken container does not resurrect\n");
    auto cfg = makeConfig("contcheck_break", 7);
    level::Level lvl(cfg);

    const int y = 100, z = 3;
    if (!putBlock(lvl, 1, y, z, (uint8_t)blk::BlockId::CHEST, 0)) {
        printf("  [FAIL] could not obtain chunk\n");
        ++failures;
        return;
    }
    check(lvl.getTile(1, y, z) != nullptr, "the chest is hydrated first");
    lvl.save();

    // Break it, exactly like Player::handleBlockBreak does: block to air
    // plus removeTile().
    putBlock(lvl, 1, y, z, (uint8_t)blk::BlockId::AIR, 0);
    lvl.removeTile(1, y, z);
    check(lvl.getTile(1, y, z) == nullptr,
          "no tile at the position once the block is broken");
    lvl.save();
}

// ---- 6. cooking ----------------------------------------------------------
static void testCooking() {
    printf("6. the furnace actually smelts\n");
    inventory::CraftingManager recipes;
    const int registered = recipes.registerBuiltinRecipes();
    check(registered > 0, "the built-in recipe table loaded");
    check(recipes.matchFurnace((uint16_t)cursestone::item::COBBLE) != nullptr,
          "cobblestone has a smelting recipe");

    const level::Position pos(8.0, 100.0, 8.0);

    // 6a. cobblestone + coal -> stone
    {
        tile::TileFurnace f(pos, &recipes);
        f.setItem(tile::TileFurnace::SLOT_INPUT,
                  item::ItemStack{(uint16_t)cursestone::item::COBBLE, 0, 1});
        f.setItem(tile::TileFurnace::SLOT_FUEL,
                  item::ItemStack{ITEM_COAL, 0, 1});
        for (uint64_t i = 0; i < 250; ++i) f.tick(i);

        auto res = f.getItem(tile::TileFurnace::SLOT_RESULT);
        check(res.id == (uint16_t)cursestone::item::STONE && res.count == 1,
              "250 ticks later the result slot holds 1 stone");
        check(f.getItem(tile::TileFurnace::SLOT_INPUT).isEmpty(),
              "the cobblestone was consumed");
        check(f.getItem(tile::TileFurnace::SLOT_FUEL).isEmpty(),
              "the coal was consumed");
    }

    // 6b. the input alone, with no fuel, must never smelt.
    {
        tile::TileFurnace f(pos, &recipes);
        f.setItem(tile::TileFurnace::SLOT_INPUT,
                  item::ItemStack{(uint16_t)cursestone::item::COBBLE, 0, 1});
        for (uint64_t i = 0; i < 600; ++i) f.tick(i);
        check(f.getItem(tile::TileFurnace::SLOT_RESULT).isEmpty(),
              "no fuel, no result after 600 ticks");
        check(f.getItem(tile::TileFurnace::SLOT_INPUT).count == 1,
              "no fuel, the input is still sitting there");
    }

    // 6c. fuel alone, with nothing smeltable, must not be burned away.
    {
        tile::TileFurnace f(pos, &recipes);
        f.setItem(tile::TileFurnace::SLOT_FUEL,
                  item::ItemStack{ITEM_COAL, 0, 1});
        for (uint64_t i = 0; i < 600; ++i) f.tick(i);
        check(f.getItem(tile::TileFurnace::SLOT_FUEL).count == 1,
              "nothing to smelt, the coal is not wasted");
    }

    // 6d. a non-recipe input does not get consumed either.
    {
        tile::TileFurnace f(pos, &recipes);
        f.setItem(tile::TileFurnace::SLOT_INPUT,
                  item::ItemStack{(uint16_t)cursestone::item::DIAMOND, 0, 2});
        f.setItem(tile::TileFurnace::SLOT_FUEL,
                  item::ItemStack{ITEM_COAL, 0, 1});
        for (uint64_t i = 0; i < 600; ++i) f.tick(i);
        check(f.getItem(tile::TileFurnace::SLOT_RESULT).isEmpty(),
              "a diamond produces nothing in a furnace");
        check(f.getItem(tile::TileFurnace::SLOT_INPUT).count == 2,
              "  ...and is not consumed");
    }

    // 6e. A cook cycle smelts exactly ONE item (vanilla behaviour, and what
    //     Infinity's Furnace.php does: the input is decremented by one when
    //     the progress bar completes), so 4 cobblestone take 4 cycles.
    {
        tile::TileFurnace f(pos, &recipes);
        f.setItem(tile::TileFurnace::SLOT_INPUT,
                  item::ItemStack{(uint16_t)cursestone::item::COBBLE, 0, 4});
        f.setItem(tile::TileFurnace::SLOT_FUEL,
                  item::ItemStack{ITEM_COAL, 0, 1});
        for (uint64_t i = 0; i < 250; ++i) f.tick(i);
        check(f.getItem(tile::TileFurnace::SLOT_RESULT).count == 1,
              "one cook cycle (200 ticks) smelts exactly one item");
        for (uint64_t i = 250; i < 900; ++i) f.tick(i);
        check(f.getItem(tile::TileFurnace::SLOT_RESULT).count == 4,
              "4 cobblestone across 4 cook cycles give 4 stone");
        check(f.getItem(tile::TileFurnace::SLOT_INPUT).isEmpty(),
              "  ...and the input stack ends up empty");
    }
}

// ---- 7. the block shows the flame (61 <-> 62) ----------------------------
// Level::setFurnaceFlame does the swap; TileFurnace::setFlame is what calls
// it from tick(), so this covers both the helper and the caller.
static void testFlame() {
    printf("7. the furnace block switches between FURNACE and BURNING_FURNACE\n");
    auto cfg = makeConfig("contcheck_flame", 1234);
    auto lvl = std::make_shared<level::Level>(cfg);

    const int x = 2, y = 100, z = 6;
    putBlock(*lvl, x, y, z, (uint8_t)blk::BlockId::FURNACE, 2);
    check(lvl->getBlockId(x, y, z) == (uint8_t)blk::BlockId::FURNACE,
          "starts as an unlit FURNACE");

    check(lvl->setFurnaceFlame(x, y, z, true), "lighting it reports a change");
    check(lvl->getBlockId(x, y, z) == (uint8_t)blk::BlockId::BURNING_FURNACE,
          "the block is now BURNING_FURNACE (62)");
    check(lvl->getBlockData(x, y, z) == 2, "the facing data survived the swap");
    check(!lvl->setFurnaceFlame(x, y, z, true),
          "lighting an already-lit furnace reports no change");

    check(lvl->setFurnaceFlame(x, y, z, false), "extinguishing it changes back");
    check(lvl->getBlockId(x, y, z) == (uint8_t)blk::BlockId::FURNACE,
          "the block is FURNACE (61) again");
    check(!lvl->setFurnaceFlame(x, y, z, false),
          "extinguishing an unlit furnace reports no change");

    // Now drive it from the tile's tick, which is the real path.
    Server& srv = Server::getInstance();
    srv.setDefaultLevel(lvl);
    inventory::CraftingManager recipes;
    recipes.registerBuiltinRecipes();
    tile::TileFurnace f(level::Position((double)x, (double)y, (double)z),
                        &recipes);
    f.setItem(tile::TileFurnace::SLOT_INPUT,
              item::ItemStack{(uint16_t)cursestone::item::COBBLE, 0, 1});
    f.setItem(tile::TileFurnace::SLOT_FUEL,
              item::ItemStack{ITEM_COAL, 0, 1});

    bool everLit = false;
    for (uint64_t i = 0; i < 260; ++i) {
        f.tick(i);
        if (lvl->getBlockId(x, y, z) == (uint8_t)blk::BlockId::BURNING_FURNACE)
            everLit = true;
    }
    check(everLit,
          "ticking a lit furnace turns its block into BURNING_FURNACE");
    check(f.getItem(tile::TileFurnace::SLOT_RESULT).count == 1,
          "  ...while it also smelts the cobblestone into stone");

    // Burn the coal out (1600 ticks) with nothing left to smelt.
    bool wentOut = false;
    for (uint64_t i = 0; i < 2000; ++i) {
        f.tick(1000 + i);
        if (lvl->getBlockId(x, y, z) == (uint8_t)blk::BlockId::FURNACE) {
            wentOut = true;
            break;
        }
    }
    check(wentOut,
          "when the fuel runs out the block goes back to FURNACE (61)");
    srv.setDefaultLevel(nullptr);
}

// ---- 8. container window ids --------------------------------------------
// Infinity hands container windows out from 2 upward (Player::addWindow:
// max(2, ++windowCnt % 99)). CurseStone used to hardcode 1, which the
// reference server never sends.
static void testWindowIds() {
    printf("8. container window ids come from the 2..98 range\n");
    PlayerConfig cfg;
    cfg.username = "harness";
    cfg.uuid = "00000000-0000-0000-0000-000000000000";
    Player p(&Server::getInstance(), cfg);

    check(p.containerWindowId() == 0,
          "a fresh player has no container window (0)");

    std::set<uint8_t> seen;
    bool anyOne = false, outOfRange = false;
    for (int i = 0; i < 300; ++i) {
        const uint8_t id = p.openContainerWindow();
        seen.insert(id);
        if (id == 1) anyOne = true;
        if (id < 2 || id > 98) outOfRange = true;
    }
    check(!anyOne, "window id 1 is never handed out");
    check(!outOfRange, "every window id stays within 2..98");
    check(seen.size() == 97,
          "the cycle covers all 97 ids from 2 to 98");
    // NB: keep the returned id in a variable — comparing containerWindowId()
    // against an inline openContainerWindow() call would read the value on
    // whichever side the compiler evaluates first.
    const uint8_t last = p.openContainerWindow();
    check(p.containerWindowId() == last,
          "containerWindowId() reports the id just handed out");
}

// ---- 9. the real server tick path cooks, and hydration stays cheap -------
// Tests 6/7 call TileFurnace::tick() by hand. This one goes through
// Level::tickTiles(), which is what Server::run() actually calls every
// tick, so it also proves a hydrated furnace is reachable from the game
// loop and not just from a unit test.
//
// The hydrated tile is replaced by one built with an explicit
// CraftingManager because a hydrated TileFurnace resolves recipes through
// Server::craftingManager(), and that is only populated by
// Server::start() — which opens a network interface, too much for a test
// binary. The dispatch being verified here is independent of where the
// recipes come from.
static void testTickTilesPath() {
    printf("9. Level::tickTiles() (the server tick path) drives the furnace\n");
    auto cfg = makeConfig("contcheck_ticks", 31337);
    auto lvl = std::make_shared<level::Level>(cfg);
    Server& srv = Server::getInstance();
    srv.setDefaultLevel(lvl);

    inventory::CraftingManager recipes;
    recipes.registerBuiltinRecipes();

    // A village-scale chunk: several chests plus one furnace, all as raw
    // blocks, exactly what the structure generator leaves behind.
    const int y = 100, z = 4;
    int chests = 0;
    for (int x = 0; x < 12; ++x)
        if (putBlock(*lvl, x, y, z, (uint8_t)blk::BlockId::CHEST, 0)) ++chests;
    const int fx = 14;
    if (!putBlock(*lvl, fx, y, z, (uint8_t)blk::BlockId::FURNACE, 2)) {
        printf("  [FAIL] could not obtain chunk\n");
        ++failures;
        srv.setDefaultLevel(nullptr);
        return;
    }
    check(chests == 12, "placed 12 generated chests in one chunk");

    // Hydration has to put the furnace in tiles_ or the game loop never
    // reaches it.
    check(lvl->getTilesInChunk(0, 0).size() == 13,
          "hydration registered 13 block entities the tick loop can see");

    auto hydrated =
        std::dynamic_pointer_cast<tile::TileFurnace>(lvl->getTile(fx, y, z));
    check(hydrated != nullptr, "the generated furnace has a TileFurnace");

    // Same tile, but with recipes we control.
    auto furnace = std::make_shared<tile::TileFurnace>(
        level::Position((double)fx, (double)y, (double)z), &recipes);
    furnace->setItem(tile::TileFurnace::SLOT_INPUT,
                     item::ItemStack{(uint16_t)cursestone::item::COBBLE, 0, 1});
    furnace->setItem(tile::TileFurnace::SLOT_FUEL,
                     item::ItemStack{ITEM_COAL, 0, 1});
    lvl->setTile(fx, y, z, furnace);

    size_t ticked = 0;
    for (uint64_t t = 0; t < 250; ++t) ticked = lvl->tickTiles(t);
    check(ticked == 13,
          "the server tick loop walked every hydrated block entity");
    check(furnace->getItem(tile::TileFurnace::SLOT_RESULT).count == 1,
          "the furnace cooked through Level::tickTiles(), not just by hand");
    check(lvl->getBlockId(fx, y, z) == (uint8_t)blk::BlockId::BURNING_FURNACE,
          "  ...and its block is showing the flame (62)");

    // Cost: hydration means tickTiles() now walks a few tiles per chunk that
    // carry no state (Tile::tick() is a no-op for everything but the
    // furnace). Measure it rather than assume.
    const auto t0 = std::chrono::steady_clock::now();
    for (uint64_t t = 0; t < 1000; ++t) lvl->tickTiles(t);
    const auto t1 = std::chrono::steady_clock::now();
    const double us =
        std::chrono::duration<double, std::micro>(t1 - t0).count() / 1000.0;
    printf("        tickTiles(): %.2f us/tick with 13 block entities\n", us);
    check(us < 100.0,
          "ticking the hydrated tiles costs well under 100 us per tick");

    srv.setDefaultLevel(nullptr);
}

int main() {
    printf("== container_check ==\n");
    testTileFactory();
    testHydration();
    testLazyHydration();
    testHydrationAfterReload();
    testNoClobber();
    testNoResurrect();
    testCooking();
    testFlame();
    testWindowIds();
    testTickTilesPath();
    printf("\n%d checks, %d failure(s)\n", checks, failures);
    printf("%s\n", failures ? "CONTAINER CHECK: FAIL" : "CONTAINER CHECK: OK");
    return failures ? 1 : 0;
}