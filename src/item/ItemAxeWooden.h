#pragma once
#include "ItemTool.h"
namespace mcppeserver::item {
class ItemAxeWooden : public ItemTool {
public:
    ItemAxeWooden() : ItemTool(271, ToolType::AXE, ToolMaterial::TOOL_WOOD) {}
    std::string name() const override { return "ItemAxe Wooden"; }
};
} // namespace
