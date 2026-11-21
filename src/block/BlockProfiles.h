/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// BlockProfiles — three behavior profiles for palette ids that real
// worlds carry but that never got a bespoke flyweight class. The
// vanilla generator decorates plains and forests with tall grass (31),
// dead bushes (32), flowers (37/38), mushrooms (39/40) and sugarcane
// (83), swamps grow vines (106) and lily pads (111), double plants
// (175) top the grass, and doors (64/71) plus beds (26) arrive through
// item placement. Registering them keeps Block::get() answers exact:
//
// - BlockPlantLike: ground cover the client walks straight through.
//   Transparent to light, walk-through, replaceable (water and player
//   placements flow into the cell), breaks instantly. A saved join
//   spot inside one of these must never read as solid ground — the
//   join settle used to dereference the null flyweight there (the
//   reported join crash).
// - BlockThinDecor: transparent walk-through cells that are NOT
//   replaceable. They never eat skylight and never count as support,
//   but neither water nor a placement overwrites them.
// - BlockSolidLike: a plain full solid block for ids with no special
//   server behavior. Same answers the generic unknown fallback hands
//   out; registering just gives the registry the exact id and name.
#pragma once
#include "Block.h"
#include <cstdint>
#include <string>
namespace cursestone::block {
class BlockPlantLike final : public Block {
  public:
    BlockPlantLike(uint8_t id, const char* name) : id_(id), name_(name) {}
    uint8_t id() const override {
        return id_;
    }
    std::string name() const override {
        return name_;
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
    float getHardness() const override {
        return 0.0f;
    }

  private:
    uint8_t id_;
    const char* name_;
};

class BlockThinDecor final : public Block {
  public:
    BlockThinDecor(uint8_t id, const char* name) : id_(id), name_(name) {}
    uint8_t id() const override {
        return id_;
    }
    std::string name() const override {
        return name_;
    }
    bool isSolid() const override {
        return false;
    }
    bool isTransparent() const override {
        return true;
    }
    float getHardness() const override {
        return 0.0f;
    }

  private:
    uint8_t id_;
    const char* name_;
};

class BlockSolidLike final : public Block {
  public:
    BlockSolidLike(uint8_t id, const char* name) : id_(id), name_(name) {}
    uint8_t id() const override {
        return id_;
    }
    std::string name() const override {
        return name_;
    }

  private:
    uint8_t id_;
    const char* name_;
};
} // namespace
