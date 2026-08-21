#pragma once
#include "ItemTool.h"
namespace mcppeserver::item {
class ItemHoeWooden : public ItemTool {
public:
    ItemHoeWooden() : ItemTool(290, ToolType::HOE, ToolMaterial::TOOL_WOOD) {}
    std::string name() const override { return "ItemHoe Wooden"; }
};
} // namespace
