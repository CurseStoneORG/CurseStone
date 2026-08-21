#pragma once
#include "ItemTool.h"
namespace mcppeserver::item {
class ItemHoeStone : public ItemTool {
public:
    ItemHoeStone() : ItemTool(291, ToolType::HOE, ToolMaterial::TOOL_STONE) {}
    std::string name() const override { return "ItemHoe Stone"; }
};
} // namespace
