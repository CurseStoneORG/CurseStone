
#include "vgen/feature/PumpkinFeature.h"
#include "vgen/Random.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"

namespace vgen {

bool PumpkinFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	for (auto i : range(64)) {
		UNUSED_VARIABLE(i);
		const int randX = random.nextGaussianInt(8);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		BlockPos pumpkinPos = pos.offset(randX, randY, randZ);
		if (region.isEmptyBlock(pumpkinPos) && region.getBlockID(pumpkinPos.below()) == Block::mGrass->mID) {
			if (Block::mPumpkin->mayPlace(region, pumpkinPos)) {
				_setBlockAndData( region, pumpkinPos, {Block::mPumpkin->mID, (DataID)random.nextInt(4)});
			}
		}
	}

	return true;
}

} // namespace vgen

