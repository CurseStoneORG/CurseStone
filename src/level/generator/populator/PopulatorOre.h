// PopulatorOre — places ore veins using OreGenerator.
#pragma once
#include "Populator.h"
#include "level/generator/object/OreGenerator.h"
#include <vector>
namespace mcppeserver::level::generator::populator {
class PopulatorOre : public Populator {
public:
    explicit PopulatorOre(std::vector<object::OreVein> veins) : veins_(std::move(veins)) {}
    const char* name() const override { return "ore"; }
    void populate(GenerationContext& ctx) override;
private:
    std::vector<object::OreVein> veins_;
};
} // namespace
