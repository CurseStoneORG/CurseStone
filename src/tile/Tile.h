// Tile — block entity base. Tiles live in chunks as CompoundTag
// entries on disk and are synced to clients via BlockEntityDataPacket.
// Each Tile subclass (TileChest, TileFurnace, TileSign) holds its
// runtime state and writes back to NBT on save.
//
// NOTE on the tag type: this project's NBT layer (src/nbt/) is pure C
// (`mcp_tag_t`, extern "C" — see nbt/tag/CompoundTag.h). writeNBT/readNBT
// use `mcp_tag_t*` directly instead of the `class CompoundTag*` this
// header used to forward-declare. That old forward-declare pointed at
// a C++ class that was never defined anywhere in the project (a leftover
// from an earlier design pass) — every real NBT compound in this
// codebase, including the one Anvil already reads/writes per-chunk
// (see level/format/anvil/AnvilChunkCodec.c), is an `mcp_tag_t` of type
// MCP_TAG_COMPOUND. Using the real type here is what lets writeNBT/
// readNBT actually be called with the tags AnvilChunkCodec hands them,
// instead of only type-checking against a phantom class that could
// never receive a real object without an unsafe cast.
#pragma once
#include <cstdint>
#include <memory>
#include <string>           // required for std::string id_ and Tile(std::string, ...)
#include "level/Position.h"
#include "nbt/tag/Tag.h"    // mcp_tag_t
namespace atomikpe::tile {
class Tile {
public:
    Tile(std::string id, atomikpe::level::Position pos)
        : id_(std::move(id)), pos_(pos) {}
    virtual ~Tile() = default;
    const std::string& getId() const { return id_; }
    const atomikpe::level::Position& position() const { return pos_; }
    void markDirty() { dirty_ = true; }
    bool isDirty() const { return dirty_; }
    // Clears the dirty flag after a successful write to the chunk's
    // tile_entities array (Level::syncTilesToChunks calls this once a
    // tile's NBT has actually been handed to mcp_chunk_add_tile_entity —
    // NOT before, so a tile whose owning chunk isn't loaded stays dirty
    // and gets retried on the next save()).
    void clearDirty() { dirty_ = false; }
    virtual void tick(uint64_t /*current_tick*/) {}
    // Serialise tile state into `tag` (a fresh MCP_TAG_COMPOUND owned by
    // the caller — Level::syncTilesToChunks() creates it, hands it here,
    // then hands it to mcp_chunk_add_tile_entity()). Base implementation
    // writes the fields every tile needs regardless of subclass: block
    // position (x/y/z) and the "id" string PMMP/Nukkit-style loaders key
    // on ("Chest", "Furnace", "Sign", ...). Subclasses override, call
    // Tile::writeNBT() first, then add their own fields.
    virtual void writeNBT(mcp_tag_t* tag) const;
    // Read tile state back from `tag`. Base implementation is a no-op
    // (x/y/z/id are already known from how the tile was constructed —
    // Level::ensureTilesLoaded() reads x/y/z itself to decide which
    // position to construct the Tile at, before readNBT ever runs).
    virtual void readNBT(const mcp_tag_t* tag);
protected:
    std::string id_;                  // "Chest", "Furnace", "Sign", ...
    atomikpe::level::Position pos_;
    bool dirty_ = true;
};
} // namespace
