#pragma once
#include "ItemTool.h"
#include <string>
namespace cursestone::item {
class ItemAxeDiamond : public ItemTool {
public:
    ItemAxeDiamond() : ItemTool(279, ToolType::AXE, ToolMaterial::TOOL_DIAMOND) {}
    std::string name() const override { return "ItemAxe Diamond"; }
};
} // namespace
