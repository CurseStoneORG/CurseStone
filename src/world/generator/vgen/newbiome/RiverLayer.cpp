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

#include "vgen/newbiome/RiverLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

RiverLayer::RiverLayer(RandomSeed seedMixup, LayerPtr& parent) :
	Layer(seedMixup) {
	mParent = parent;
}

void RiverLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	int px = xo - 1;
	int py = yo - 1;
	int pw = w + 2;
	int ph = h + 2;
	mParent->fillArea(layerData, px, py, pw, ph);

	for(auto y : range(h)) {
		for(auto x : range(w)) {
			int l = riverFilter(layerData.mParentArea[(x + 0) + (y + 1) * pw]);
			int r = riverFilter(layerData.mParentArea[(x + 2) + (y + 1) * pw]);
			int u = riverFilter(layerData.mParentArea[(x + 1) + (y + 0) * pw]);
			int d = riverFilter(layerData.mParentArea[(x + 1) + (y + 2) * pw]);
			int c = riverFilter(layerData.mParentArea[(x + 1) + (y + 1) * pw]);
			if(c != l || c != u || c != r || c != d) {
				layerData.mResult[x + y * w] = Biome::river->mId;
			}
			else {
				layerData.mResult[x + y * w] = -1;
			}
		}
	}
	layerData.swap();
}

int RiverLayer::riverFilter(int value) {
	if(value >= 2) {
		return 2 + (value & 1);
	}
	return value;
}

} // namespace vgen

