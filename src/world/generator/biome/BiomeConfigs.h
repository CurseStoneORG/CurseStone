// SPDX-License-Identifier: LGPL-3.0-or-later
// BiomeConfigs — accessors for every biome configuration file.
// Each biome lives in its own .cpp under biome/.
#pragma once
#include "BiomeConfig.h"

namespace cursestone::level::generator::biome {

const BiomeConfig& oceanBiome();
const BiomeConfig& beachBiome();
const BiomeConfig& plainsBiome();
const BiomeConfig& plainsPlateauBiome();
const BiomeConfig& forestBiome();
const BiomeConfig& desertBiome();
const BiomeConfig& taigaBiome();
const BiomeConfig& savannaBiome();
const BiomeConfig& jungleBiome();
const BiomeConfig& swampBiome();
const BiomeConfig& icePlainsBiome();
const BiomeConfig& mountainsBiome();

} // namespace cursestone::level::generator::biome
