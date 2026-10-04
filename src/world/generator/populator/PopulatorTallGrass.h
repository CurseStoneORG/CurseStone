#pragma once
#include "Populator.h"
namespace cursestone::world::generator::populator {
class PopulatorTallGrass : public Populator {
public:
    explicit PopulatorTallGrass(int attempts_per_chunk) : attempts_(attempts_per_chunk) {}
    const char* name() const override { return "tall_grass"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};
} // namespace
