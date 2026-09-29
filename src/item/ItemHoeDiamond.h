#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemHoeDiamond : public ItemTool {
public:
    ItemHoeDiamond() : ItemTool(293, ToolType::HOE, ToolMaterial::TOOL_DIAMOND) {}
    std::string name() const override { return "ItemHoe Diamond"; }
};
} // namespace
