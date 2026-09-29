#pragma once
#include "Populator.h"
namespace cursestone::level::generator::populator {
class PopulatorCave : public Populator {
public:
    explicit PopulatorCave(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "cave"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};
} // namespace
