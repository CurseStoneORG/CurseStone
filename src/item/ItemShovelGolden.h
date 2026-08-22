#pragma once
#include "ItemTool.h"
#include <string>
namespace mcppeserver::item {
class ItemShovelGolden : public ItemTool {
public:
    ItemShovelGolden() : ItemTool(284, ToolType::SHOVEL, ToolMaterial::TOOL_GOLD) {}
    std::string name() const override { return "ItemShovel Golden"; }
};
} // namespace
