#pragma once
#include "ItemTool.h"
#include <string>
namespace cenisyspe::item {
class ItemSwordWooden : public ItemTool {
public:
    ItemSwordWooden() : ItemTool(268, ToolType::SWORD, ToolMaterial::TOOL_WOOD) {}
    std::string name() const override { return "ItemSword Wooden"; }
};
} // namespace
