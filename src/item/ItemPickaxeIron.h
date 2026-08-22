#pragma once
#include "ItemTool.h"
#include <string>
namespace mcppeserver::item {
class ItemPickaxeIron : public ItemTool {
public:
    ItemPickaxeIron() : ItemTool(257, ToolType::PICKAXE, ToolMaterial::TOOL_IRON) {}
    std::string name() const override { return "ItemPickaxe Iron"; }
};
} // namespace
