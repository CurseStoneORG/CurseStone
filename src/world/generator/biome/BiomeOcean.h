// BiomeOcean — bioma oceano.
#pragma once
#include "Biome.h"
namespace cursestone::world::generator::biome {
class BiomeOcean : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "ocean"; }
    float temperature() const override { return 0.5f; }
    float rainfall()    const override { return 0.5f; }
    // Fondo de grava: el oceano es lo bastante profundo como para que no
    // haya capa de tierra debajo de la arena.
    uint8_t topBlock()   const override { return 13; } // gravel
    uint8_t fillerBlock() const override { return 13; } // gravel
};
} // namespace
