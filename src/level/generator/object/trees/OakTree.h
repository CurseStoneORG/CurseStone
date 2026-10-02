// SPDX-License-Identifier: LGPL-3.0-or-later
// OakTree — vanilla small oak (the GenericTree default shape).
// Ported from ext-vanillagenerator's GenericTree default species.
#pragma once
#include "GenericTree.h"

namespace cursestone::level::generator::object::trees {

class OakTree : public GenericTree {
protected:
    void init(GenRandom& rng) override {
        height_ = 4 + (int)rng.nextInt(3);
        logId_ = 17;  logData_ = 0;   // LOG, oak
        leafId_ = 18; leafData_ = 0;  // LEAVES, oak
    }
};

} // namespace cursestone::level::generator::object::trees
