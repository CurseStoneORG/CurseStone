/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */

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

