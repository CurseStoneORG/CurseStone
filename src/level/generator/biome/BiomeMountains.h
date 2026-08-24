// BiomeMountains — concrete biome.
#pragma once
#include "Biome.h"
namespace atomikpe::level::generator::biome {
class BiomeMountains : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "mountains"; }
};
} // namespace
