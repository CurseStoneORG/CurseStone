#include "Tile.h"
#include "nbt/tag/CompoundTag.h"
namespace cursestone::tile {
void Tile::writeNBT(mcp_tag_t* tag) const {
    if (!tag) return;
    mcp_compound_tag_set_string(tag, "id", id_.c_str());
    mcp_compound_tag_set_int(tag, "x", static_cast<int32_t>(pos_.x));
    mcp_compound_tag_set_int(tag, "y", static_cast<int32_t>(pos_.y));
    mcp_compound_tag_set_int(tag, "z", static_cast<int32_t>(pos_.z));
}
void Tile::readNBT(const mcp_tag_t* /*tag*/) {
    // x/y/z/id are already known by the time a subclass's readNBT runs
    // (World::ensureTilesLoaded reads them from the tag itself to decide
    // where to construct the Tile, before construction happens) — nothing
    // left to do at the base-class level.
}
} // namespace
