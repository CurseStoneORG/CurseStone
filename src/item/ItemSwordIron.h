#pragma once
#include "ItemTool.h"
#include <string>
namespace cenisyspe::item {
class ItemSwordIron : public ItemTool {
public:
    ItemSwordIron() : ItemTool(267, ToolType::SWORD, ToolMaterial::TOOL_IRON) {}
    std::string name() const override { return "ItemSword Iron"; }
};
} // namespace
