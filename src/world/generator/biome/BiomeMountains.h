// BiomeMountains — bioma de montaña (extreme hills).
#pragma once
#include "Biome.h"
namespace cursestone::world::generator::biome {
class BiomeMountains : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "mountains"; }
    float temperature() const override { return 0.2f; }
    float rainfall()    const override { return 0.3f; }
    // Sin capa de tierra: la roca llega hasta la superficie.
    uint8_t topBlock()   const override { return 1; } // stone
    uint8_t fillerBlock() const override { return 1; } // stone
};
} // namespace
