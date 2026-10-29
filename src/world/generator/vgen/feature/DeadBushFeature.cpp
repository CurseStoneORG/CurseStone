
#include "vgen/feature/DeadBushFeature.h"
#include "vgen/material/Material.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/Random.h"

namespace vgen {

bool DeadBushFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	BlockID t(0);
	BlockPos blockPos(pos);
	const Material* currentMaterial = nullptr;

	while (t != Block::mDeadBush->mID && (currentMaterial == nullptr || _isFree(*currentMaterial)) && blockPos.y > 0) {
		currentMaterial = &region.getMaterial(blockPos);
		blockPos = blockPos.below();
	}

	for (auto i : range(4)) {
		UNUSED_VARIABLE(i);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos bushPos = blockPos.offset(randX, randY, randZ);

		if (region.isEmptyBlock(bushPos)) {
			if (Block::mDeadBush->canSurvive(region, bushPos)) {
				_setBlockAndData( region, bushPos, Block::mDeadBush->mID);
			}
		}
	}

	return true;
}

bool DeadBushFeature::_isFree(const Material& material) const {
	return material.isType(MaterialType::Air)
		|| material.isType(MaterialType::Leaves);
}

} // namespace vgen

