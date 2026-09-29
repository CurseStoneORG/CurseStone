#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemShovelWooden : public ItemTool {
public:
    ItemShovelWooden() : ItemTool(269, ToolType::SHOVEL, ToolMaterial::TOOL_WOOD) {}
    std::string name() const override { return "ItemShovel Wooden"; }
};
} // namespace
