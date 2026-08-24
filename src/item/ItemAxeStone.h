#pragma once
#include "ItemTool.h"
#include <string>
namespace atomikpe::item {
class ItemAxeStone : public ItemTool {
public:
    ItemAxeStone() : ItemTool(275, ToolType::AXE, ToolMaterial::TOOL_STONE) {}
    std::string name() const override { return "ItemAxe Stone"; }
};
} // namespace
