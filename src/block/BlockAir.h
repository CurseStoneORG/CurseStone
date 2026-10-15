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
#include "Block.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockAir : public Block {
  public:
    uint8_t id() const override {
        return AIR;
    }
    bool isSolid() const override {
        return false;
    }
    bool isTransparent() const override {
        return true;
    }
    bool canBeReplaced() const override {
        return true;
    }
    bool canWalkThrough() const override {
        return true;
    }
    float getHardness() const override {
        return 0;
    }
    std::string name() const override {
        return "Air";
    }
    cursestone::math::AxisAlignedBB getBoundingBox() const override {
        return {0, 0, 0, 0, 0, 0};
    }
};
} // namespace
