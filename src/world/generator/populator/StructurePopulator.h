// SPDX-License-Identifier: LGPL-3.0-or-later
// StructurePopulator — stamps fixed templates (desert temples, and later
// whatever else gets added) into the terrain.
//
// Placement follows vanilla's rigid structure placement: the world is cut
// into regions of `kSpacing` x `kSpacing` chunks, every region holds at
// most ONE candidate chunk, the candidate is kept away from the region
// edge by `kSeparation`, and where it lands inside the region comes from
// a PRNG seeded only by (seed, regionX, regionZ). Nothing about it
// depends on which chunk is being generated or in which order chunks are
// generated, so a temple lands in exactly the same spot every time — and
// it is never "half generated", because every chunk re-derives the whole
// candidate list and writes only its own share of the blocks that fall
// inside it.
#pragma once
#include "Populator.h"
#include "world/generator/structure/Structure.h"
#include <memory>
#include <vector>

namespace cursestone::world::generator::populator {

class StructurePopulator final : public Populator {
public:
    StructurePopulator() = default;

    // Register a structure to place. Every registered structure is tried
    // for every candidate chunk, so several may share a spot.
    void add(std::shared_ptr<structure::Structure> s) {
        structures_.push_back(std::move(s));
    }

    const char* name() const override { return "Structure"; }
    void populate(GenerationContext& ctx) override;

private:
    std::vector<std::shared_ptr<structure::Structure>> structures_;
};

} // namespace cursestone::world::generator::populator