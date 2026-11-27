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
// BinaryHeap — the A* open set.
//
// Port of <minecraftpe/impl/entity/path/BinaryHeap.cpp> (MCPE 0.8.1
// decompile): a min-heap of node pointers keyed by PathNode::f, with
// the node's heap position tracked inside the node so changeCost can
// repair in place (the era nodePositionInHeapMaybe).
#pragma once
#include "entity/ai/path/PathNode.h"
#include <vector>

namespace cursestone::entity::ai {

class BinaryHeap {
  public:
    BinaryHeap() {
        heap_.reserve(1024);
    }

    void clear() { heap_.clear(); }
    int32_t size() const { return (int32_t)heap_.size(); }
    bool isEmpty() const { return heap_.empty(); }

    /// @brief The cheapest node (era peek).
    PathNode* peek() const { return heap_.empty() ? nullptr : heap_.front(); }

    /// @brief Era insert: push and up-heap.
    void insert(PathNode* node) {
        node->heap_index = (int32_t)heap_.size();
        heap_.push_back(node);
        upHeap((int32_t)heap_.size() - 1);
    }

    /// @brief Era pop (dequeue): removes and returns the cheapest node.
    PathNode* pop() {
        if (heap_.empty())
            return nullptr;
        PathNode* top = heap_.front();
        heap_[0] = heap_.back();
        heap_[0]->heap_index = 0;
        heap_.pop_back();
        if (!heap_.empty())
            downHeap(0);
        return top;
    }

    /// @brief Era changeCost: the node's f dropped; repair in place.
    void changeCost(PathNode* node) {
        upHeap(node->heap_index);
    }

  private:
    void upHeap(int32_t i) {
        PathNode* node = heap_[(size_t)i];
        const float cost = node->f;
        while (i > 0) {
            const int32_t parent = (i - 1) >> 1;
            PathNode* p = heap_[(size_t)parent];
            if (cost >= p->f)
                break;
            heap_[(size_t)i] = p;
            p->heap_index = i;
            i = parent;
        }
        heap_[(size_t)i] = node;
        node->heap_index = i;
    }

    void downHeap(int32_t i) {
        PathNode* node = heap_[(size_t)i];
        const float cost = node->f;
        const int32_t count = (int32_t)heap_.size();
        while (true) {
            const int32_t left = 2 * i + 1;
            const int32_t right = 2 * i + 2;
            if (left >= count)
                break;
            const int32_t child =
                (right < count && heap_[(size_t)right]->f < heap_[(size_t)left]->f)
                    ? right
                    : left;
            PathNode* c = heap_[(size_t)child];
            if (c->f >= cost)
                break;
            heap_[(size_t)i] = c;
            c->heap_index = i;
            i = child;
        }
        heap_[(size_t)i] = node;
        node->heap_index = i;
    }

    std::vector<PathNode*> heap_;
};

} // namespace cursestone::entity::ai
