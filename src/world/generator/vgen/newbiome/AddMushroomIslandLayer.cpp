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

#include "vgen/newbiome/AddMushroomIslandLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

AddMushroomIslandLayer::AddMushroomIslandLayer(RandomSeed seedMixup, LayerPtr& parent) :
	Layer(seedMixup) {
	mParent = parent;
}

void AddMushroomIslandLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	const int px = xo - 1;
	const int py = yo - 1;
	const int pw = w + 2;
	const int ph = h + 2;

	mParent->fillArea(layerData, px, py, pw, ph);

	for(auto y : range(h)) {
		for(auto x : range(w)) {
			const int n1 = layerData.mParentArea[(x + 0) + (y + 0) * pw];
			const int n2 = layerData.mParentArea[(x + 2) + (y + 0) * pw];
			const int n3 = layerData.mParentArea[(x + 0) + (y + 2) * pw];
			const int n4 = layerData.mParentArea[(x + 2) + (y + 2) * pw];
			const int c = layerData.mParentArea[(x + 1) + (y + 1) * pw];

			initRandom(x + xo, y + yo);
			if(c == 0 && (n1 == 0 && n2 == 0 && n3 == 0 && n4 == 0) && nextRandom(100) == 0) {
				layerData.mResult[x + y * w] = Biome::mushroomIsland->mId;
			}
			else {
				layerData.mResult[x + y * w] = c;
			}
		}
	}
	layerData.swap();
}

} // namespace vgen

