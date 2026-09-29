#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemAxeGolden : public ItemTool {
public:
    ItemAxeGolden() : ItemTool(286, ToolType::AXE, ToolMaterial::TOOL_GOLD) {}
    std::string name() const override { return "ItemAxe Golden"; }
};
} // namespace
