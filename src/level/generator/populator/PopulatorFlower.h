#pragma once
#include "Populator.h"
#include <cstdint>
namespace cenisyspe::level::generator::populator {
class PopulatorFlower : public Populator {
public:
    PopulatorFlower(uint8_t flower_block, int attempts)
        : flower_(flower_block), attempts_(attempts) {}
    const char* name() const override { return "flower"; }
    void populate(GenerationContext& ctx) override;
private:
    uint8_t flower_;
    int attempts_;
};
} // namespace
