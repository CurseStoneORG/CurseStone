// CurseStone | vgen — foliage/grass color helpers.
//
// The vanilla implementation samples a texture palette; the palette
// asset does not exist on this server, so these use the classic
// corner-lerp formula over the vanilla anchor colors (spruce/birch
// keep their fixed vanilla values).
#pragma once
#include "vgen/Vanilla.h"

namespace vgen {

class FoliageColor {
public:
	static int getGrassColor(float temp, float rain);
	static int getFoliageColor(float temp, float rain);
	static int getMapGrassColor(float temp, float rain);
	static int getMapFoliageColor(float temp, float rain);
	static int getEvergreenColor(float temp, float rain);
	static int getBirchColor(float temp, float rain);
	static int getMapEvergreenColor(float temp, float rain);
	static int getMapBirchColor(float temp, float rain);
	static int getDefaultColor();
};

} // namespace vgen
