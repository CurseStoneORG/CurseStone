#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemPickaxeDiamond : public ItemTool {
public:
    ItemPickaxeDiamond() : ItemTool(278, ToolType::PICKAXE, ToolMaterial::TOOL_DIAMOND) {}
    std::string name() const override { return "ItemPickaxe Diamond"; }
};
} // namespace
