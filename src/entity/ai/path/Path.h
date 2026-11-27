/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

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
// SPDX-License-Identifier: LGPL-3.0-or-later
// Path — one searched route as a list of cells.
//
// Port of <minecraftpe/impl/entity/path/Path.cpp> (MCPE 0.8.1
// decompile). The era Path deep-copied the A* result nodes; the port
// stores them by value (same cells, no per-node allocation).
#pragma once
#include "entity/ai/path/PathNode.h"
#include <cstdint>
#include <vector>

namespace cursestone::entity::ai {

class Mob;

class Path {
  public:
    /// @brief Builds from the A* reconstruct result (start -> goal).
    explicit Path(std::vector<PathNode> cells) : cells_(std::move(cells)) {}

    /// @brief True when the walk index passed the last cell.
    bool isDone() const { return index_ >= (int32_t)cells_.size(); }

    int32_t getSize() const { return (int32_t)cells_.size(); }
    void setSize(int32_t size) {
        if (size < 0)
            size = 0;
        if (size > (int32_t)cells_.size())
            size = (int32_t)cells_.size();
        cells_.resize((size_t)size);
    }
    int32_t getIndex() const { return index_; }
    void setIndex(int32_t i) { index_ = i; }
    void next() { ++index_; }

    /// @brief Cell at @p i, or nullptr out of range (era Path::get).
    const PathNode* get(int32_t i) const {
        if (i < 0 || i >= (int32_t)cells_.size())
            return nullptr;
        return &cells_[(size_t)i];
    }

    /// @brief The final cell, or nullptr on an empty path.
    const PathNode* last() const {
        return cells_.empty() ? nullptr : &cells_.back();
    }

    /// @brief Era Path::getPos: the walk target of cell @p i — the cell
    ///        center plus the wide-body offset ((int)(width + 1)) / 2.
    struct WalkPos {
        double x, y, z;
    };
    WalkPos getPos(float entityWidth, int32_t i) const {
        const PathNode& n = cells_[(size_t)i];
        const float off = (float)((int32_t)(entityWidth + 1.0f)) * 0.5f;
        return WalkPos{n.xCoord + off, (double)n.yCoord, n.zCoord + off};
    }

    WalkPos currentPos(float entityWidth) const {
        return getPos(entityWidth, index_);
    }

    /// @brief Era sameAs: identical cell lists (the navigation keeps a
    ///        recomputed path only when it differs from the current one).
    bool sameAs(const Path* other) const;

  private:
    std::vector<PathNode> cells_;
    int32_t index_ = 0;
};

} // namespace cursestone::entity::ai
