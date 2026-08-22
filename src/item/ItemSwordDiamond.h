#pragma once
#include "ItemTool.h"
#include <string>
namespace mcppeserver::item {
class ItemSwordDiamond : public ItemTool {
public:
    ItemSwordDiamond() : ItemTool(276, ToolType::SWORD, ToolMaterial::TOOL_DIAMOND) {}
    std::string name() const override { return "ItemSword Diamond"; }
};
} // namespace
