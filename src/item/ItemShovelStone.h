#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemShovelStone : public ItemTool {
public:
    ItemShovelStone() : ItemTool(273, ToolType::SHOVEL, ToolMaterial::TOOL_STONE) {}
    std::string name() const override { return "ItemShovel Stone"; }
};
} // namespace
