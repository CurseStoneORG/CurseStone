#pragma once
#include "ItemTool.h"
#include <string>
namespace cenisyspe::item {
class ItemPickaxeGolden : public ItemTool {
public:
    ItemPickaxeGolden() : ItemTool(285, ToolType::PICKAXE, ToolMaterial::TOOL_GOLD) {}
    std::string name() const override { return "ItemPickaxe Golden"; }
};
} // namespace
