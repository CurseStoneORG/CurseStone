/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#pragma once
#include "Item.h"
#include <cstdint>
namespace cursestone::item {
enum ToolMaterial : int {
    TOOL_WOOD = 0, TOOL_GOLD = 1, TOOL_STONE = 2, TOOL_IRON = 3, TOOL_DIAMOND = 4,
};
enum ToolType : int {
    PICKAXE = 0, AXE = 1, SHOVEL = 2, SWORD = 3, HOE = 4,
};
class ItemTool : public Item {
public:
    ItemTool(uint16_t id, ToolType t, ToolMaterial m)
        : Item(id), type_(t), material_(m) {}
    bool isTool() const override { return true; }
    int maxStackSize() const override { return 1; }
    ToolType type() const { return type_; }
    ToolMaterial material() const { return material_; }
    int attackDamage() const override {
        switch (material_) {
            case TOOL_WOOD:   return 2;
            case TOOL_GOLD:   return 2;
            case TOOL_STONE:  return 4;
            case TOOL_IRON:   return 6;
            case TOOL_DIAMOND:return 8;
        }
        return 1;
    }
private:
    ToolType type_;
    ToolMaterial material_;
};
} // namespace
