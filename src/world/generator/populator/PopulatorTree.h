#pragma once
#include "Populator.h"
#include "world/generator/object/TreeGenerator.h"
namespace cursestone::level::generator::populator {
// Scatters trees, choosing the vanilla species for the biome under each
// candidate column (oak/birch in forests, spruce in taiga/hills, jungle in
// jungle, acacia in savanna, swamp oak in swampland...).
class PopulatorTree : public Populator {
public:
    explicit PopulatorTree(int attempts_per_chunk) : attempts_(attempts_per_chunk) {}
    const char* name() const override { return "tree"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};
} // namespace
