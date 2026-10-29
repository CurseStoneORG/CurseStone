// CurseStone | vgen — foliage/grass color implementation.
#include "vgen/FoliageColor.h"

namespace vgen {

static const int FOLIAGE_WARM_WET = 0x48B518;  // vanilla base foliage
static const int FOLIAGE_COLD_DRY = 0x6C9A47;  // olive frost tone
static const int GRASS_WARM_WET = 0x91BD59;    // vanilla base grass
static const int GRASS_COLD_DRY = 0x86B783;    // frost grass tone
static const int EVERGREEN = 0x619961;         // vanilla spruce constant
static const int BIRCH = 0x80A755;             // vanilla birch constant

static int clamp255(float v) {
	int i = (int)(v * 255.0f);
	if (i < 0) i = 0;
	if (i > 255) i = 255;
	return i;
}

static int lerpColor(int a, int b, float f) {
	f = Math::clamp(f, 0.0f, 1.0f);
	int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
	int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
	int rr = (int)(ar + (br - ar) * f);
	int rg = (int)(ag + (bg - ag) * f);
	int rb = (int)(ab + (bb - ab) * f);
	return (rr << 16) | (rg << 8) | rb;
}

// Classic biome color formula: t in [0,1], r' = rain * t in [0,1];
// the colder/drier the corner, the more the frost tone shows.
static float cornerFactor(float temp, float rain) {
	return Math::clamp(temp, 0.0f, 1.0f) * Math::clamp(rain, 0.0f, 1.0f);
}

int FoliageColor::getFoliageColor(float temp, float rain) {
	return lerpColor(FOLIAGE_COLD_DRY, FOLIAGE_WARM_WET, cornerFactor(temp, rain));
}
int FoliageColor::getGrassColor(float temp, float rain) {
	return lerpColor(GRASS_COLD_DRY, GRASS_WARM_WET, cornerFactor(temp, rain));
}
int FoliageColor::getMapFoliageColor(float temp, float rain) {
	return getFoliageColor(temp, rain);
}
int FoliageColor::getMapGrassColor(float temp, float rain) {
	return getGrassColor(temp, rain);
}
int FoliageColor::getEvergreenColor(float temp, float rain) {
	(void)temp;
	(void)rain;
	return EVERGREEN;
}
int FoliageColor::getBirchColor(float temp, float rain) {
	(void)temp;
	(void)rain;
	return BIRCH;
}
int FoliageColor::getMapEvergreenColor(float temp, float rain) {
	return getEvergreenColor(temp, rain);
}
int FoliageColor::getMapBirchColor(float temp, float rain) {
	return getBirchColor(temp, rain);
}
int FoliageColor::getDefaultColor() {
	return FOLIAGE_WARM_WET;
}

} // namespace vgen
