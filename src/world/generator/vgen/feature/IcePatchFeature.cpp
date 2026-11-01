
#include "vgen/feature/IcePatchFeature.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/Random.h"

namespace vgen {

IcePatchFeature::IcePatchFeature(int radius) :
	mBlock(Block::mPackedIce->mID)
	, mRadius(radius) {

}

bool IcePatchFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	BlockPos blockPos(pos);
	while (region.isEmptyBlock(blockPos) && blockPos.y > 2) {
		blockPos = blockPos.below();
	}

	BlockID currentBlock = region.getBlockID(blockPos);
	if (currentBlock != Block::mSnow->mID || currentBlock == Block::mPackedIce->mID) {
		return false;
	}

	int r = random.nextInt(mRadius - 2) + 2;
	int yr = 1;

	for (auto xx : range_incl(blockPos.x - r, blockPos.x + r)) {
		for (auto zz : range_incl(blockPos.z - r, blockPos.z + r)) {
			int xd = xx - blockPos.x;
			int zd = zz - blockPos.z;
			if (xd * xd + zd * zd > r * r) {
				continue;
			}

			for (auto yy : range_incl(blockPos.y - yr, blockPos.y + yr)) {
				BlockPos iceBlock(xx, yy, zz);
				BlockID t = region.getBlockID(iceBlock);
				if (t == Block::mDirt->mID || t == Block::mSnow->mID || t == Block::mIce->mID) {
					_setBlockAndData( region, iceBlock, mBlock);
				}
			}
		}
	}

	return true;
}

} // namespace vgen

