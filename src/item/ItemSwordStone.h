#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemSwordStone : public ItemTool {
public:
    ItemSwordStone() : ItemTool(272, ToolType::SWORD, ToolMaterial::TOOL_STONE) {}
    std::string name() const override { return "ItemSword Stone"; }
};
} // namespace
