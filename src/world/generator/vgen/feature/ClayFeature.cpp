
#include "vgen/feature/ClayFeature.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/material/Material.h"
#include "vgen/Random.h"

namespace vgen {

ClayFeature::ClayFeature(int radius) :
	mRadius(radius) {

}

bool ClayFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	const Block& block = region.getBlock(pos);
	auto& mat = block.getMaterial();
	if (!mat.isType(MaterialType::Water) && !block.isType(Block::mClay)) {
		return false;
	}

	int r = random.nextInt(mRadius - 2) + 2;
	int yr = 1;

	for (auto xx : range_incl(pos.x - r, pos.x + r)) {
		for (auto zz : range_incl(pos.z - r, pos.z + r)) {
			int xd = xx - pos.x;
			int zd = zz - pos.z;
			if (xd * xd + zd * zd > r * r) {
				continue;
			}

			for (auto yy : range_incl(pos.y - yr, pos.y + yr)) {
				BlockPos clayPos(xx, yy, zz);
				BlockID targetBlock = region.getBlockID(clayPos);
				if (targetBlock == Block::mDirt->mID) {
					_setBlockAndData( region, clayPos, Block::mClay->mID);
				}
			}
		}
	}

	return true;
}

} // namespace vgen

