// BiomeOcean — concrete biome.
#pragma once
#include "Biome.h"
namespace mcppeserver::level::generator::biome {
class BiomeOcean : public Biome {
public:
    uint8_t id() const override;
    const char* name() const override { return "ocean"; }
};
} // namespace
