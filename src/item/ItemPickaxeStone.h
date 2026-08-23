#pragma once
#include "ItemTool.h"
#include <string>
namespace cenisyspe::item {
class ItemPickaxeStone : public ItemTool {
public:
    ItemPickaxeStone() : ItemTool(274, ToolType::PICKAXE, ToolMaterial::TOOL_STONE) {}
    std::string name() const override { return "ItemPickaxe Stone"; }
};
} // namespace
