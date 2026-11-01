
#include "vgen/feature/JungleTreeFeature.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"

namespace vgen {

JungleTreeFeature::JungleTreeFeature(Entity* placer) 
	: TreeFeature(placer, enum_cast(OldLogBlock::LogType::Jungle), enum_cast(OldLeafBlock::LeafType::Jungle), true) {

}

bool JungleTreeFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	int treeHeight = random.nextInt(7) + 4;
	return TreeFeature::place(region, pos, random, treeHeight);
}

} // namespace vgen

