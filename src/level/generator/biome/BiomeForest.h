// BiomeForest — concrete biome.
#pragma once
#include "Biome.h"
namespace cursestone::level::generator::biome {
class BiomeForest : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "forest"; }
};
} // namespace
