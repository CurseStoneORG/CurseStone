#include "Biome.h"
#include "BiomePlains.h"
#include "BiomeDesert.h"
#include "BiomeForest.h"
#include "BiomeMountains.h"
#include "BiomeOcean.h"
namespace mcppeserver::level::generator::biome {
uint8_t BiomePlains::id() const    { return BIOME_PLAINS; }
uint8_t BiomeDesert::id() const    { return BIOME_DESERT; }
uint8_t BiomeForest::id() const    { return BIOME_FOREST; }
uint8_t BiomeMountains::id() const { return BIOME_EXTREME_HILLS; }
uint8_t BiomeOcean::id() const     { return BIOME_OCEAN; }
} // namespace
