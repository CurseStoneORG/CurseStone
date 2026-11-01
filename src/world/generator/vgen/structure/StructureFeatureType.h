/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/
#pragma once
#include "vgen/Vanilla.h"

namespace vgen {

enum class StructureFeatureType : int8_t {
	EndCity,
	Fortress,
	Mineshaft,
	Monument,
	Stronghold,
	Temple,
	Village,
	Unknown
};

} // namespace vgen

