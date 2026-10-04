// SPDX-License-Identifier: LGPL-3.0-or-later
// JungleTree — vanilla jungle tree (tall, jungle log/leaf data = 3).
// Ported from ext-vanillagenerator's JungleTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::level::generator::object::trees {

class JungleTree : public GenericTree {
protected:
    void init(GenRandom& rng) override {
        height_ = 4 + (int)rng.nextInt(7);   // 4..10, reference JungleTree
        logId_ = 17;  logData_ = 3;   // LOG, jungle
        leafId_ = 18; leafData_ = 3;  // LEAVES, jungle
    }
};

} // namespace cursestone::level::generator::object::trees
