#include "TileSign.h"
#include "nbt/tag/CompoundTag.h"
namespace cursestone::tile {
// PMMP/Nukkit legacy key each line as "Text1".."Text4" (plain strings,
// not JSON — that's a Bedrock-modern-protocol thing this legacy-84
// project doesn't use).
void TileSign::writeNBT(mcp_tag_t* tag) const {
    if (!tag) return;
    Tile::writeNBT(tag);
    mcp_compound_tag_set_string(tag, "Text1", lines_[0].c_str());
    mcp_compound_tag_set_string(tag, "Text2", lines_[1].c_str());
    mcp_compound_tag_set_string(tag, "Text3", lines_[2].c_str());
    mcp_compound_tag_set_string(tag, "Text4", lines_[3].c_str());
    // Se persiste el Creator (Genisys lo borra al guardar, pero acá se
    // conserva a propósito para que el dueño siga pudiendo editar el
    // cartel después de reiniciar el servidor).
    mcp_compound_tag_set_string(tag, "Creator", creator_.c_str());
}
void TileSign::readNBT(const mcp_tag_t* tag) {
    if (!tag) return;
    Tile::readNBT(tag);
    lines_[0] = mcp_compound_tag_get_string(tag, "Text1", "");
    lines_[1] = mcp_compound_tag_get_string(tag, "Text2", "");
    lines_[2] = mcp_compound_tag_get_string(tag, "Text3", "");
    lines_[3] = mcp_compound_tag_get_string(tag, "Text4", "");
    creator_  = mcp_compound_tag_get_string(tag, "Creator", "");
    dirty_ = false;
}
} // namespace cursestone::tile
