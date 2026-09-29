#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemHoeIron : public ItemTool {
public:
    ItemHoeIron() : ItemTool(292, ToolType::HOE, ToolMaterial::TOOL_IRON) {}
    std::string name() const override { return "ItemHoe Iron"; }
};
} // namespace
