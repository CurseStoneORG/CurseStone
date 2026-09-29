#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemShovelDiamond : public ItemTool {
public:
    ItemShovelDiamond() : ItemTool(277, ToolType::SHOVEL, ToolMaterial::TOOL_DIAMOND) {}
    std::string name() const override { return "ItemShovel Diamond"; }
};
} // namespace
