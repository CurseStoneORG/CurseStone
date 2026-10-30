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

#include "vgen/v1/LargeFeature.h"
#include "vgen/VgenWorld.h"
#include "vgen/biome/BiomeSource.h"
#include "vgen/VgenWorld.h"
#include "vgen/VgenWorld.h"

namespace vgen {

LargeFeature::LargeFeature() :
	radius(8){
}

LargeFeature::~LargeFeature(){
}

void LargeFeature::apply(LevelChunk& lc, Dimension& dimension, Random& random){
	int r = radius;

	random.setSeed(dimension.getBiomes().getSeed());
	int xScale = random.nextInt() / 2 * 2 + 1;
	int zScale = random.nextInt() / 2 * 2 + 1;

	auto& lcPosition = lc.getPosition();
	for (int x = lcPosition.x - r; x <= lcPosition.x + r; x++) {
		for (int z = lcPosition.z - r; z <= lcPosition.z + r; z++) {
			random.setSeed((x * xScale + z * zScale) ^ dimension.getBiomes().getSeed());
			addFeature(lc, dimension, random, x , z );
		}
	}
}

} // namespace vgen

