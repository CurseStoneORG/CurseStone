
#include "vgen/feature/BirchFeature.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"
#include "vgen/Random.h"

namespace vgen {

BirchFeature::BirchFeature(Entity* placer, bool superBirch) :
	TreeFeature(placer, enum_cast(OldLogBlock::LogType::Birch), enum_cast(OldLeafBlock::LeafType::Birch))
	, mSuperBirch(superBirch) {

}

bool BirchFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	int treeHeight = random.nextInt(3) + 5;
	if (mSuperBirch) {
		treeHeight += random.nextInt(7);
	}

	return TreeFeature::place(region, pos, random, treeHeight);
}

} // namespace vgen

