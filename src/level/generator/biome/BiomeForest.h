// BiomeForest — bioma de bosque.
#pragma once
#include "Biome.h"
namespace cursestone::level::generator::biome {
class BiomeForest : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "forest"; }
    float temperature() const override { return 0.7f; }
    float rainfall()    const override { return 0.8f; }
    uint8_t topBlock()   const override { return 2; } // grass
    uint8_t fillerBlock() const override { return 3; } // dirt
};
} // namespace
