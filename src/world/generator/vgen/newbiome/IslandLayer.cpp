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

#include "vgen/newbiome/IslandLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

IslandLayer::IslandLayer(RandomSeed seedMixup)
	: Layer(seedMixup) {
}

void IslandLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	for(auto y : range(h)) {
		for(auto x : range(w)) {
			initRandom(xo + x, yo + y);
			layerData.mResult[x + y * w] = (nextRandom(10) == 0) ? 1 : 0;
		}
	}
	// if (0, 0) is located here, place an island
	if(xo > -w && xo <= 0 && yo > -h && yo <= 0) {
		layerData.mResult[-xo + -yo * w] = 1;
	}
	layerData.swap();
}

} // namespace vgen

