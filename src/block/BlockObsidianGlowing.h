#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockObsidianGlowing : public BlockSolid {
public:
    uint8_t id() const override { return OBSIDIAN_GLOWING; }
    std::string name() const override { return "Glowing Obsidian"; }
};
} // namespace
