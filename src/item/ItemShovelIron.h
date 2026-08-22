#pragma once
#include "ItemTool.h"
#include <string>
namespace mcppeserver::item {
class ItemShovelIron : public ItemTool {
public:
    ItemShovelIron() : ItemTool(256, ToolType::SHOVEL, ToolMaterial::TOOL_IRON) {}
    std::string name() const override { return "ItemShovel Iron"; }
};
} // namespace
