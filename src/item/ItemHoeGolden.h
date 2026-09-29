#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemHoeGolden : public ItemTool {
public:
    ItemHoeGolden() : ItemTool(294, ToolType::HOE, ToolMaterial::TOOL_GOLD) {}
    std::string name() const override { return "ItemHoe Golden"; }
};
} // namespace
