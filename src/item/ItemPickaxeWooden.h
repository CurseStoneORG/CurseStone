#pragma once
#include "ItemTool.h"
#include <string>
namespace cenisyspe::item {
class ItemPickaxeWooden : public ItemTool {
public:
    ItemPickaxeWooden() : ItemTool(270, ToolType::PICKAXE, ToolMaterial::TOOL_WOOD) {}
    std::string name() const override { return "ItemPickaxe Wooden"; }
};
} // namespace
