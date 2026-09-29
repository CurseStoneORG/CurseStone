#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemSwordGolden : public ItemTool {
public:
    ItemSwordGolden() : ItemTool(283, ToolType::SWORD, ToolMaterial::TOOL_GOLD) {}
    std::string name() const override { return "ItemSword Golden"; }
};
} // namespace
