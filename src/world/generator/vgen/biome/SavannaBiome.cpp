#include "MobSpawnUtils.h"
#include "vgen/biome/SavannaBiome.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/Vanilla.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/feature/DoublePlantFeature.h"

namespace vgen {

SavannaBiome::SavannaBiome(int id) :
	Biome(id, Biome::BiomeType::Savanna) {
	_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::Horse, 1, 2, 6, MobSpawnUtils::spawnHorseHerd));

	decorator->treeCount = 1;
	decorator->flowerCount = 4;
	decorator->grassCount = 20;
}

const FeaturePtr& SavannaBiome::getTreeFeature(Random* random){
	if(random->nextInt(5) > 0) {
		return decorator->savannaTreeFeature;
	}
	return decorator->oakFeature;
}

void SavannaBiome::decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit){
	if (!legacy) {
		const int x = random.nextInt(16);
		const int z = random.nextInt(16);
		BlockPos pos = origin.offset(x, 0, z);
		pos.y = source->getHeightmap(pos.x, pos.z);
		decorator->doublePlantFeature->place(*source, pos, random, enum_cast(DoublePlantType::Grass));
	}
	Biome::decorate(source, random, origin, legacy, limit);
}


} // namespace vgen

