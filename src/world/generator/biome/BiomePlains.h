// BiomePlains — bioma de llanura.
#pragma once
#include "Biome.h"
namespace cursestone::level::generator::biome {
class BiomePlains : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "plains"; }
    float temperature() const override { return 0.8f; }
    float rainfall()    const override { return 0.4f; }
    uint8_t topBlock()   const override { return 2; } // grass
    uint8_t fillerBlock() const override { return 3; } // dirt
};
} // namespace
