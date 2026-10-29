
#include "vgen/feature/WaterlilyFeature.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/Random.h"

namespace vgen {

bool WaterlilyFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	for (auto i : range(10)) {
		UNUSED_VARIABLE(i);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos lilyPos = pos.offset(randX, randY, randZ);

		if (region.isEmptyBlock(lilyPos) && Block::mWaterlily->mayPlace(region, lilyPos)) {
			_setBlockAndData( region, lilyPos, Block::mWaterlily->mID);
		}
	}

	return true;
}

} // namespace vgen

