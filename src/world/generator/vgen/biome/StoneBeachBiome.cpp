#include "vgen/biome/StoneBeachBiome.h"
#include "vgen/Blocks.h"
#include "vgen/biome/BiomeDecorator.h"

namespace vgen {

StoneBeachBiome::StoneBeachBiome(int id) :
	Biome(id, Biome::BiomeType::StoneBeach) {
	// remove default mob spawn settings
	_friendlies.clear();
	mTopMaterial = Block::mStone->mID;
	mMaterial = Block::mStone->mID;

	decorator->treeCount = -999;
	decorator->deadBushCount = 0;
	decorator->reedsCount = 0;
	decorator->cactusCount = 0;
}

} // namespace vgen

