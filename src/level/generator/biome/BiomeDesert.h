// BiomeDesert — bioma de desierto.
#pragma once
#include "Biome.h"
namespace cursestone::level::generator::biome {
class BiomeDesert : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "desert"; }
    // Muy caluroso y sin lluvia: el generador lo usa para decidir
    // cactus y arena en vez dePasto.
    float temperature() const override { return 2.0f; }
    float rainfall()    const override { return 0.0f; }
    uint8_t topBlock()   const override { return 12; } // sand
    uint8_t fillerBlock() const override { return 24; } // sandstone
};
} // namespace
