// BiomeDesert — concrete biome.
#pragma once
#include "Biome.h"
namespace cursestone::level::generator::biome {
class BiomeDesert : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "desert"; }
};
} // namespace
