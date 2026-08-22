#pragma once
#include "ItemTool.h"
namespace mcppeserver::item {
class ItemHoeIron : public ItemTool {
public:
    ItemHoeIron() : ItemTool(292, ToolType::HOE, ToolMaterial::TOOL_IRON) {}
    std::string name() const override { return "ItemHoe Iron"; }
};
} // namespace
