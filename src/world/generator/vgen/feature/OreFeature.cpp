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

#include "vgen/feature/OreFeature.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/Vanilla.h"
#include "vgen/Random.h"

namespace vgen {

OreFeature::OreFeature(BlockID block, int count) :
	mBlock(block, 0)
	, mCount(count) {

}

OreFeature::OreFeature(BlockID block, DataID blockData, int count) :
	mBlock(block, blockData)
	, mCount(count) {

}

bool OreFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {

	float dir = random.nextFloat() * Math::PI;
	auto rot = Vec3(Math::sin(dir), 0, Math::cos(dir));
	rot *= (float)mCount / 8.0f;

	Vec3 v0 = Vec3(pos) + Vec3(8.0f + rot.x, (float)random.nextInt(3) - 2.0f, 8.0f + rot.z);
	Vec3 v1 = Vec3(pos) + Vec3(8.0f - rot.x, (float)random.nextInt(3) - 2.0f, 8.0f - rot.z);

	BlockPos blockPos;

	for (auto D : range(mCount)) {
		const float d = (float)D;
		const Vec3 vv = v0 + (v1 - v0) * d / (float)mCount;

		const float ss = random.nextFloat() * mCount / 16;
		const float r = (Math::sin(d * Math::PI / mCount) + 1) * ss + 1;
		const float halfR = r / 2.f;
		const float invHalfR = 1.f / (r / 2);

		const BlockPos t0 = vv - halfR;
		const BlockPos t1 = vv + halfR;

		for (blockPos.x = t0.x; blockPos.x <= t1.x; blockPos.x++) {
			float xd = ((blockPos.x + 0.5f) - vv.x) * invHalfR;

			if (xd * xd < 1) {
				for (blockPos.y = t0.y; blockPos.y <= t1.y; blockPos.y++) {

					float yd = ((blockPos.y + 0.5f) - vv.y) * invHalfR;
					if (xd * xd + yd * yd < 1) {

						for (blockPos.z = t0.z; blockPos.z <= t1.z; blockPos.z++) {
							float zd = ((blockPos.z + 0.5f) - vv.z) * invHalfR;

							if (xd * xd + yd * yd + zd * zd < 1) {
								if (_isDiggable(region.getBlockID(blockPos))) {
									_setBlockAndData( region, blockPos, mBlock);
								}
							}
						}
					}
				}
			}
		}
	}

	return true;
}

bool OreFeature::_isDiggable(BlockID block) {
	return block == Block::mStone->mID ||
		   block == Block::mNetherrack->mID;
}

} // namespace vgen

