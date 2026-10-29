
#include "vgen/feature/SandFeature.h"
#include "vgen/material/Material.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/Vanilla.h"
#include "vgen/Random.h"

namespace vgen {

SandFeature::SandFeature(BlockID block, int radius)
	: mBlock(block)
	, mRadius(radius) {
}

bool SandFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	if (region.getMaterial(pos) != Material::getMaterial(MaterialType::Water)) {
		return false;
	}

	int r = random.nextInt(mRadius - 2) + 2;
	int yr = 2;
	for (auto xx : range_incl(pos.x - r, pos.x + r)) {
		for (auto zz : range_incl(pos.z - r, pos.z + r)) {
			int xd = xx - pos.x;
			int zd = zz - pos.z;
			if (xd * xd + zd * zd > r * r) {
				continue;
			}

			for (auto yy : range_incl(pos.y - yr, pos.y + yr)) {
				BlockPos sandPos(xx, yy, zz);
				BlockID t = region.getBlockID(sandPos);
				if (t == Block::mDirt->mID || t == Block::mGrass->mID) {
					_setBlockAndData( region, sandPos, mBlock);
				}
			}
		}
	}

	return true;
}

} // namespace vgen

