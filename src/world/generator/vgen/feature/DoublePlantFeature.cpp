
#include "vgen/feature/DoublePlantFeature.h"
#include "vgen/Random.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/Vanilla.h"

namespace vgen {

bool DoublePlantFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	return place(region, pos, random, enum_cast(DoublePlantType::Grass));
}

bool DoublePlantFeature::place(BlockSource& region, const BlockPos& pos, Random& random, int plantType) const {
	bool placedPlant = false;

	for (auto i : range(64)) {
		UNUSED_VARIABLE(i);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos plantPos = pos.offset(randX, randY, randZ);

		if (region.isEmptyBlock(plantPos) && plantPos.y < (region.getMaxHeight() - 1 - 1)) {
			if (Block::mDoublePlant->mayPlace(region, plantPos)) {
				static_cast<const DoublePlantBlock*>(Block::mDoublePlant)->placeAt(region, plantPos, plantType, Block::UPDATE_CLIENTS, nullptr);
				placedPlant = true;
			}
		}
	}

	return placedPlant;
}

} // namespace vgen

