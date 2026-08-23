#pragma once
#include "ItemTool.h"
#include <string>
namespace cenisyspe::item {
class ItemAxeIron : public ItemTool {
public:
    ItemAxeIron() : ItemTool(258, ToolType::AXE, ToolMaterial::TOOL_IRON) {}
    std::string name() const override { return "ItemAxe Iron"; }
};
} // namespace
