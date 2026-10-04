// SPDX-License-Identifier: LGPL-3.0-or-later
// BirchTree — vanilla birch (taller, birch log/leaf data = 2).
// Ported from ext-vanillagenerator's BirchTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::level::generator::object::trees {

class BirchTree : public GenericTree {
protected:
    void init(GenRandom& rng) override {
        height_ = 5 + (int)rng.nextInt(3);
        logId_ = 17;  logData_ = 2;   // LOG, birch
        leafId_ = 18; leafData_ = 2;  // LEAVES, birch
    }
};

} // namespace cursestone::level::generator::object::trees
